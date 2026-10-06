#include "core/level_navigation.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}
bool bounded(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool coordinate(double value) { return bounded(value, -1.0e7, 1.0e7); }
bool vectorValid(const std::array<double, 3>& v) { return coordinate(v[0]) && coordinate(v[1]) && coordinate(v[2]); }
QJsonArray vectorJson(const std::array<double, 3>& v) { return {v[0], v[1], v[2]}; }
bool number(const QJsonObject& object, const char* key, double* out)
{
	const auto value = object.value(QLatin1String(key));
	if (!value.isDouble() || !std::isfinite(value.toDouble())) { return false; }
	*out = value.toDouble();
	return true;
}
bool integer(const QJsonObject& object, const char* key, int* out)
{
	double value = 0;
	if (!number(object, key, &value) || value < 0 || value > 3 || std::floor(value) != value) { return false; }
	*out = static_cast<int>(value);
	return true;
}
bool vectorRead(const QJsonValue& value, std::array<double, 3>* out)
{
	if (!value.isArray() || value.toArray().size() != 3) { return false; }
	const auto values = value.toArray();
	for (int i = 0; i < 3; ++i) {
		if (!values[i].isDouble()) { return false; }
		(*out)[i] = values[i].toDouble();
	}
	return vectorValid(*out);
}
bool exactKeys(const QJsonObject& object, const QStringList& keys)
{
	if (object.size() != keys.size()) { return false; }
	for (const auto& key : keys) { if (!object.contains(key)) { return false; } }
	return true;
}
constexpr std::array<std::array<int, 2>, 3> planAxes {{{0, 1}, {0, 2}, {1, 2}}};
QJsonObject viewJson(const LevelViewState& view)
{
	QJsonArray plans;
	for (const auto& plan : view.plans) {
		plans.append(QJsonObject{{"projection", plan.projection}, {"center", QJsonArray{plan.center.x(), plan.center.y()}},
			{"zoom", plan.zoom}});
	}
	const auto& c = view.camera;
	return {{"layout", levelViewLayoutId(view.layout)}, {"activePlan", view.activePlan}, {"cameraVisible", view.cameraVisible},
		{"links", QJsonObject{{"centers", view.links.centers}, {"zoom", view.links.zoom}, {"followCamera", view.links.followCamera}}},
		{"plans", plans}, {"camera", QJsonObject{{"perspective", c.perspective}, {"position", vectorJson(c.position)},
			{"yaw", c.yaw}, {"pitch", c.pitch}, {"fieldOfView", c.fieldOfView}, {"focusDistance", c.focusDistance},
			{"orbitTarget", vectorJson(c.orbitTarget)}, {"orbitYaw", c.orbitYaw}, {"orbitPitch", c.orbitPitch}, {"orbitScale", c.orbitScale}}}};
}
bool viewRead(const QJsonObject& object, LevelViewState* view, int version)
{
	QStringList keys {"layout", "activePlan", "cameraVisible", "plans", "camera"};
	if (version >= 2) { keys << QStringLiteral("links"); }
	if (!exactKeys(object, keys) ||
		!object.value("layout").isString() || !levelViewLayoutForId(object.value("layout").toString(), &view->layout) ||
		!integer(object, "activePlan", &view->activePlan) || !object.value("cameraVisible").isBool() ||
		!object.value("plans").isArray() || object.value("plans").toArray().size() != 3 || !object.value("camera").isObject()) { return false; }
	view->cameraVisible = object.value("cameraVisible").toBool();
	if (version >= 2) {
		if (!object.value("links").isObject()) { return false; }
		const auto links = object.value("links").toObject();
		if (!exactKeys(links, {"centers", "zoom", "followCamera"}) || !links.value("centers").isBool()
			|| !links.value("zoom").isBool() || !links.value("followCamera").isBool()) { return false; }
		view->links = {links.value("centers").toBool(), links.value("zoom").toBool(), links.value("followCamera").toBool()};
	}
	const auto plans = object.value("plans").toArray();
	for (int i = 0; i < 3; ++i) {
		if (!plans[i].isObject()) { return false; }
		const auto plan = plans[i].toObject();
		if (!exactKeys(plan, {"projection", "center", "zoom"}) || !integer(plan, "projection", &view->plans[i].projection) ||
			!number(plan, "zoom", &view->plans[i].zoom) || !plan.value("center").isArray()) { return false; }
		const auto center = plan.value("center").toArray();
		if (center.size() != 2 || !center[0].isDouble() || !center[1].isDouble()) { return false; }
		view->plans[i].center = QPointF(center[0].toDouble(), center[1].toDouble());
	}
	const auto camera = object.value("camera").toObject();
	auto& c = view->camera;
	if (!exactKeys(camera, {"perspective", "position", "yaw", "pitch", "fieldOfView", "focusDistance",
		"orbitTarget", "orbitYaw", "orbitPitch", "orbitScale"}) || !camera.value("perspective").isBool() ||
		!vectorRead(camera.value("position"), &c.position) || !vectorRead(camera.value("orbitTarget"), &c.orbitTarget) ||
		!number(camera, "yaw", &c.yaw) || !number(camera, "pitch", &c.pitch) || !number(camera, "fieldOfView", &c.fieldOfView) ||
		!number(camera, "focusDistance", &c.focusDistance) || !number(camera, "orbitYaw", &c.orbitYaw) ||
		!number(camera, "orbitPitch", &c.orbitPitch) || !number(camera, "orbitScale", &c.orbitScale)) { return false; }
	c.perspective = camera.value("perspective").toBool();
	return validateLevelViewState(*view);
}
} // namespace

bool centerLevelPlanNavigation(std::array<PlanViewState, 3>* plans, const std::array<double, 3>& point, QString* error)
{
	if (!plans || !vectorValid(point)) { return fail(error, QCoreApplication::translate("LevelNavigation", "The linked view centre is outside the supported coordinate range.")); }
	for (const auto& plan : *plans) { if (!validatePlanViewState(plan, error)) { return false; } }
	for (auto& plan : *plans) {
		const auto axes = planAxes[plan.projection];
		plan.center = QPointF(point[axes[0]], point[axes[1]]);
	}
	return true;
}

bool linkLevelPlanNavigation(std::array<PlanViewState, 3>* plans, int source, const LevelViewLinks& links, QString* error)
{
	if (!plans || source < 0 || source >= 3) { return fail(error, QCoreApplication::translate("LevelNavigation", "Choose an existing plan pane as the navigation source.")); }
	for (const auto& plan : *plans) { if (!validatePlanViewState(plan, error)) { return false; } }
	auto candidate = *plans;
	if (links.centers) {
		std::array<double, 3> point {};
		std::array<bool, 3> assigned {};
		const auto take = [&](const PlanViewState& plan) {
			const auto axes = planAxes[plan.projection];
			if (!assigned[axes[0]]) { point[axes[0]] = plan.center.x(); assigned[axes[0]] = true; }
			if (!assigned[axes[1]]) { point[axes[1]] = plan.center.y(); assigned[axes[1]] = true; }
		};
		take(candidate[source]);
		for (const auto& plan : candidate) { take(plan); }
		if (!centerLevelPlanNavigation(&candidate, point, error)) { return false; }
	}
	if (links.zoom) { for (auto& plan : candidate) { plan.zoom = (*plans)[source].zoom; } }
	*plans = candidate;
	return true;
}

bool validatePlanViewState(const PlanViewState& state, QString* error)
{
	return (state.projection >= 0 && state.projection < 3 && coordinate(state.center.x()) && coordinate(state.center.y()) &&
		bounded(state.zoom, 0.0025, 64.0)) || fail(error, QCoreApplication::translate("LevelNavigation", "The plan view has an invalid projection, centre or zoom."));
}
bool validateCameraViewState(const CameraViewState& state, QString* error)
{
	return (vectorValid(state.position) && vectorValid(state.orbitTarget) && bounded(state.yaw, -360000, 360000) &&
		bounded(state.pitch, -89, 89) && bounded(state.fieldOfView, 15, 150) && bounded(state.focusDistance, 1, 1.0e7) &&
		bounded(state.orbitYaw, -360000, 360000) && bounded(state.orbitPitch, -90, 90) && bounded(state.orbitScale, 0.002, 512)) ||
		fail(error, QCoreApplication::translate("LevelNavigation", "The camera view contains an invalid position, angle, field of view or scale."));
}
bool validateLevelViewState(const LevelViewState& state, QString* error)
{
	if (state.layout < LevelViewLayout::Single2D || state.layout > LevelViewLayout::FourViews || state.activePlan < 0 || state.activePlan > 2) {
		return fail(error, QCoreApplication::translate("LevelNavigation", "The saved view has an invalid layout or active pane."));
	}
	for (int i = 0; i < 3; ++i) {
		if (!validatePlanViewState(state.plans[i], error)) { return false; }
		if (state.layout == LevelViewLayout::FourViews && state.plans[i].projection != i) {
			return fail(error, QCoreApplication::translate("LevelNavigation", "Four-view bookmarks require top, front and side panes in order."));
		}
	}
	return validateCameraViewState(state.camera, error);
}
bool validateLevelBookmarks(const LevelViewBookmarks& bookmarks, QString* error)
{
	if (bookmarks.size() > LevelBookmarkLimit) { return fail(error, QCoreApplication::translate("LevelNavigation", "A map can have at most 128 saved views.")); }
	QSet<QString> ids, names;
	for (const auto& bookmark : bookmarks) {
		const auto id = QUuid(bookmark.id);
		if (id.isNull() || id.toString(QUuid::WithoutBraces) != bookmark.id || ids.contains(bookmark.id)) {
			return fail(error, QCoreApplication::translate("LevelNavigation", "Each saved view needs a unique, canonical UUID."));
		}
		const auto name = bookmark.name;
		if (name.isEmpty() || name != name.trimmed() || name.size() > 128 || names.contains(name.normalized(QString::NormalizationForm_C).toCaseFolded())) {
			return fail(error, QCoreApplication::translate("LevelNavigation", "Saved view names must be unique and contain 1–128 characters without surrounding spaces."));
		}
		for (const auto ch : name) {
			if (ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) {
				return fail(error, QCoreApplication::translate("LevelNavigation", "Saved view names cannot contain control characters or line breaks."));
			}
		}
		if (!validateLevelViewState(bookmark.view, error)) { return false; }
		ids.insert(bookmark.id);
		names.insert(name.normalized(QString::NormalizationForm_C).toCaseFolded());
	}
	return true;
}
QJsonObject levelBookmarksJson(const LevelViewBookmarks& bookmarks)
{
	QJsonArray views;
	for (const auto& bookmark : bookmarks) {
		views.append(QJsonObject{{"id", bookmark.id}, {"name", bookmark.name}, {"view", viewJson(bookmark.view)}});
	}
	return {{"format", "VibeStudioLevelViews"}, {"version", 2}, {"bookmarks", views}};
}
bool parseLevelBookmarks(const QByteArray& bytes, LevelViewBookmarks* bookmarks, QString* error)
{
	if (!bookmarks || bytes.size() > LevelBookmarkBytesLimit) { return fail(error, QCoreApplication::translate("LevelNavigation", "Saved view files are limited to 1 MiB.")); }
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(bytes, &parseError);
	const auto root = document.object();
	if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
		!exactKeys(root, {"format", "version", "bookmarks"}) || root.value("format").toString() != QStringLiteral("VibeStudioLevelViews") ||
		!root.value("version").isDouble() || (root.value("version").toDouble() != 1 && root.value("version").toDouble() != 2) || !root.value("bookmarks").isArray()) {
		return fail(error, QCoreApplication::translate("LevelNavigation", "Expected a version-1 or version-2 VibeStudio level views JSON file."));
	}
	const auto values = root.value("bookmarks").toArray();
	if (values.size() > LevelBookmarkLimit) { return fail(error, QCoreApplication::translate("LevelNavigation", "A map can have at most 128 saved views.")); }
	LevelViewBookmarks candidate;
	for (const auto& value : values) {
		const auto object = value.toObject();
		LevelViewBookmark bookmark;
		if (!value.isObject() || !exactKeys(object, {"id", "name", "view"}) || !object.value("id").isString() ||
			!object.value("name").isString() || !object.value("view").isObject() || !viewRead(object.value("view").toObject(), &bookmark.view, root.value("version").toInt())) {
			return fail(error, QCoreApplication::translate("LevelNavigation", "A saved view contains missing, unknown or invalid fields."));
		}
		bookmark.id = object.value("id").toString();
		bookmark.name = object.value("name").toString();
		candidate.append(bookmark);
	}
	if (!validateLevelBookmarks(candidate, error)) { return false; }
	*bookmarks = std::move(candidate);
	return true;
}
QString levelBookmarkStorePath(const QString& sourcePath, const QString& wadMapName)
{
	if (sourcePath.trimmed().isEmpty()) { return {}; }
	const QFileInfo source(sourcePath);
	QString identity = source.canonicalFilePath();
	if (identity.isEmpty()) { identity = QDir::cleanPath(source.absoluteFilePath()); }
#ifdef Q_OS_WIN
	identity = identity.toCaseFolded();
#endif
	identity += QLatin1Char('\n') + wadMapName.toUpper();
	const auto key = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
	const auto override = StudioSettings::overrideFilePath();
	const auto root = override.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("level-views"))
		: QFileInfo(override).absoluteFilePath() + QStringLiteral(".level-views");
	return QDir(root).filePath(key + QStringLiteral(".json"));
}
bool readLevelBookmarks(const QString& path, LevelViewBookmarks* bookmarks, QByteArray* revision, QString* error)
{
	if (!bookmarks || !revision || path.isEmpty()) { return fail(error, QCoreApplication::translate("LevelNavigation", "A saved view file path is required.")); }
	const QFileInfo info(path);
	if (info.isSymLink()) { return fail(error, QCoreApplication::translate("LevelNavigation", "Saved view storage cannot be a symbolic link.")); }
	if (!info.exists()) { *bookmarks = {}; *revision = {}; return true; }
	QFile file(path);
	if (!info.isFile() || !file.open(QIODevice::ReadOnly)) { return fail(error, QCoreApplication::translate("LevelNavigation", "Cannot read saved views: %1").arg(file.errorString())); }
	const auto bytes = file.read(LevelBookmarkBytesLimit + 1);
	if (file.error() != QFileDevice::NoError) { return fail(error, QCoreApplication::translate("LevelNavigation", "Cannot read saved views: %1").arg(file.errorString())); }
	LevelViewBookmarks candidate;
	if (!parseLevelBookmarks(bytes, &candidate, error)) { return false; }
	*bookmarks = std::move(candidate);
	*revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return true;
}
bool writeLevelBookmarks(const QString& path, const LevelViewBookmarks& bookmarks, const QByteArray& expectedRevision,
	QByteArray* newRevision, QString* error)
{
	if (path.isEmpty()) { return fail(error, QCoreApplication::translate("LevelNavigation", "A saved view file path is required.")); }
	if (!validateLevelBookmarks(bookmarks, error)) { return false; }
	const auto bytes = QJsonDocument(levelBookmarksJson(bookmarks)).toJson(QJsonDocument::Indented);
	if (bytes.size() > LevelBookmarkBytesLimit) { return fail(error, QCoreApplication::translate("LevelNavigation", "Saved view files are limited to 1 MiB.")); }
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) { return fail(error, QCoreApplication::translate("LevelNavigation", "Cannot create the saved view storage directory.")); }
	QLockFile lock(path + QStringLiteral(".lock"));
	if (!lock.tryLock(0)) { return fail(error, QCoreApplication::translate("LevelNavigation", "Another process is writing these saved views. Try again when it finishes.")); }
	LevelViewBookmarks current;
	QByteArray revision;
	if (!readLevelBookmarks(path, &current, &revision, error)) { return false; }
	if (revision != expectedRevision) { return fail(error, QCoreApplication::translate("LevelNavigation", "Saved views changed outside this editor. Reload them before saving.")); }
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		return fail(error, QCoreApplication::translate("LevelNavigation", "Cannot save views: %1").arg(file.errorString()));
	}
	if (newRevision) { *newRevision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
	return true;
}
} // namespace vibestudio
