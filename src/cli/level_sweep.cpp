#include "cli/level_sweep.h"
#include <QCoreApplication>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace vibestudio::cli
{
bool parseLevelSweepArguments(const QStringList& arguments, LevelSweepCliRequest* output, QString* error)
{
	const auto fail = [error](const char* message) {
		if (error) {
			*error = QCoreApplication::translate("LevelSweepCli", message);
		}
		return false;
	};
	if (!output) {
		return false;
	}
	*output = {};
	if (error) {
		error->clear();
	}
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> options{"--set",        "--delta",        "--axis",      "--angle",   "--scale",  "--segments",
	                            "--iterations", "--path",         "--alignment", "--texture", "--output", "--settings-file",
	                            "--locale",     "--catalog-root", "--engine",    "--map-name"};
	QMap<QString, QString> values;
	QSet<QString> seen;
	QStringList positional, sets;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) {
			positional << arg;
			continue;
		}
		const int equal = int(arg.indexOf('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != "--set") {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "A sweep option was repeated."));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!options.contains(key)) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Unknown sweep option or a value supplied to a flag."));
		}
		QString value;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith("--")) {
			value = arguments[++i];
		}
		if (value.isEmpty()) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "A sweep option value is missing."));
		}
		if (key == "--set") {
			sets << value;
		} else {
			values.insert(key, value);
		}
	}
	if (positional.size() != 3 || positional[0] != "map" || positional[1] != "sweep" || sets.isEmpty() || sets.size() > 64 ||
	    values.value("--output").isEmpty() || !validLevelClipTexture(values.value("--texture"))) {
		return fail(
		    QT_TRANSLATE_NOOP("LevelSweepCli", "Use map sweep <map> with repeated --set brush:ID/face/IDs, --texture and --output."));
	}
	LevelSweepCliRequest result;
	result.path = positional[2];
	result.output = values.value("--output");
	result.engine = values.value("--engine");
	result.mapName = values.value("--map-name");
	result.dryRun = seen.contains("--dry-run");
	result.overwrite = seen.contains("--overwrite");
	auto& r = result.sweep;
	r.texture = values.value("--texture");
	static const QRegularExpression pattern(QStringLiteral("^brush:([0-9]+)/face/([0-9]+(?:,[0-9]+)*)$"));
	int count = 0;
	for (const auto& text : sets) {
		if (text.size() > 1024) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Too many face IDs in a sweep selector."));
		}
		const auto match = pattern.match(text);
		bool valid = false;
		if (!match.hasMatch()) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep selectors must be brush:ID/face/IDs."));
		}
		const int id = match.captured(1).toInt(&valid);
		if (!valid) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "A sweep brush ID is out of range."));
		}
		LevelMapComponentSet set;
		set.object = {LevelMapSelectionKind::QuakeBrush, id};
		set.kind = LevelBrushComponent::Face;
		for (const auto& token : match.captured(2).split(',')) {
			const int face = token.toInt(&valid);
			if (!valid || ++count > 64) {
				return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Use at most 64 valid face IDs in a sweep."));
			}
			set.components << face;
		}
		r.faces << set;
	}
	const auto vector = [&](const QString& key, LevelMapVec3* target) {
		if (!values.contains(key)) {
			return true;
		}
		const auto parts = values[key].split(',', Qt::KeepEmptyParts);
		if (parts.size() != 3) {
			return false;
		}
		double v[3];
		for (int k = 0; k < 3; ++k) {
			bool valid = false;
			v[k] = parts[k].toDouble(&valid);
			if (!valid || !std::isfinite(v[k])) {
				return false;
			}
		}
		*target = {v[0], v[1], v[2], true};
		return true;
	};
	if (!vector("--delta", &r.translation) || !vector("--axis", &r.rotationAxis) || !vector("--scale", &r.scale)) {
		return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep delta, axis and scale require three finite numbers: x,y,z."));
	}
	if (values.contains("--angle")) {
		bool valid = false;
		r.angle = values["--angle"].toDouble(&valid);
		if (!valid || !std::isfinite(r.angle)) {
			return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep angle must be finite degrees."));
		}
	}
	const auto integer = [&](const QString& key, int* target, int maximum) {
		if (!values.contains(key)) {
			return true;
		}
		bool valid = false;
		const int value = values[key].toInt(&valid);
		if (!valid || value < 1 || value > maximum) {
			return false;
		}
		*target = value;
		return true;
	};
	if (!integer("--segments", &r.segments, 64) || !integer("--iterations", &r.iterations, 8)) {
		return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep uses 1–64 segments and 1–8 iterations."));
	}
	const auto path = values.value("--path", "arc");
	if (path == "arc") {
		r.path = LevelSweepPath::Arc;
	} else if (path == "straight") {
		r.path = LevelSweepPath::Straight;
	} else if (path == "s-bend") {
		r.path = LevelSweepPath::SBend;
	} else {
		return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep path must be arc, straight or s-bend."));
	}
	const auto alignment = values.value("--alignment", "integer");
	if (alignment != "integer" && alignment != "free") {
		return fail(QT_TRANSLATE_NOOP("LevelSweepCli", "Sweep alignment must be integer or free."));
	}
	r.integerAlignment = alignment == "integer";
	*output = std::move(result);
	return true;
}
} // namespace vibestudio::cli
