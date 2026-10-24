#include "core/level_clipper.h"
#include <QCoreApplication>
#include <algorithm>

namespace vibestudio {
bool validLevelClipTexture(const QString& texture)
{
	return !texture.isEmpty() && texture.size() <= 1024 && !texture.contains(QStringLiteral("//"))
		&& !texture.contains(QStringLiteral("/*")) && !texture.contains(QStringLiteral("*/"))
		&& std::none_of(texture.cbegin(),texture.cend(),[](QChar c) {
			return c.isSpace() || c.isNull() || c.category() == QChar::Other_Control || QStringLiteral("\"{}()[]\\").contains(c);
		});
}
QJsonObject levelClipperPreferencesJson(const LevelClipperPreferences& p)
{
	return {{"material",p.material == LevelClipMaterial::Inherit ? "inherit" : p.material == LevelClipMaterial::Current ? "current" : "custom"},
		{"customTexture",p.customTexture},{"resetPoints",p.resetPoints},{"resetSide",p.resetSide},{"twoPointsInPlan",p.twoPointsInPlan},
		{"wrapMerge",p.wrapMerge},{"doubleClick",p.doubleClick == LevelClipDoubleClick::None ? "none" : p.doubleClick == LevelClipDoubleClick::Clip ? "clip" : "split"}};
}
bool updateLevelClipperPreferences(const QJsonObject& values, LevelClipperPreferences* preferences, QString* error)
{
	if (!preferences) { return false; }
	auto next = *preferences;
	const auto fail = [&](const QString& field) {
		if (error) { *error = QCoreApplication::translate("LevelClipper","Invalid clipper preference: %1.").arg(field); } return false;
	};
	for (auto it = values.begin(); it != values.end(); ++it) {
		const auto key = it.key(); const auto value = it.value();
		if (key == "material") {
			const auto id = value.toString();
			if (id == "inherit") { next.material = LevelClipMaterial::Inherit; }
			else if (id == "current") { next.material = LevelClipMaterial::Current; }
			else if (id == "custom") { next.material = LevelClipMaterial::Custom; }
			else { return fail(key); }
		} else if (key == "customTexture") {
			if (!value.isString() || !validLevelClipTexture(value.toString())) { return fail(key); }
			next.customTexture = value.toString();
		} else if (key == "doubleClick") {
			const auto id = value.toString();
			if (id == "none") { next.doubleClick = LevelClipDoubleClick::None; }
			else if (id == "clip") { next.doubleClick = LevelClipDoubleClick::Clip; }
			else if (id == "split") { next.doubleClick = LevelClipDoubleClick::Split; }
			else { return fail(key); }
		} else if (key == "resetPoints" || key == "resetSide" || key == "twoPointsInPlan" || key == "wrapMerge") {
			if (!value.isBool()) { return fail(key); }
			if (key == "resetPoints") { next.resetPoints = value.toBool(); }
			else if (key == "resetSide") { next.resetSide = value.toBool(); }
			else if (key == "wrapMerge") { next.wrapMerge = value.toBool(); }
			else { next.twoPointsInPlan = value.toBool(); }
		} else { return fail(key); }
	}
	*preferences = next; return true;
}
} // namespace vibestudio
