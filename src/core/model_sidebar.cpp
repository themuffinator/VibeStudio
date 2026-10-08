#include "core/model_sidebar.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

namespace vibestudio {

namespace {

struct TabEntry {
	const char* id;
	const char* icon;
	const char* title;
	const char* description;
};

// The studio's own order and names, translated where read.
constexpr TabEntry kTabs[] = {
	{"outliner", "tree", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Outliner"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "The model's surfaces, tags, joints and collision boxes, and the components of the current mode, to select and hide.")},
	{"add", "plus", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Add"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Primitives to add as new surfaces or into the active one, and generated props.")},
	{"item", "properties", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Item"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "The selection's position, rotation and scale, typed exactly, with the pivot and frame scope.")},
	{"tool", "edit", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Tool"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Settings of the active tool: snapping, proportional editing, symmetry and the transform handles.")},
	{"surface", "layers", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Surface"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "The active surface's name, shader or skin, material slots and skin files, UV seams and mapping, and surface operations.")},
	{"animation", "film", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Animation"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Frames and clips: names, ranges, rates, in-betweens and pose copies.")},
	{"skeleton", "link", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Skeleton"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Joints, skeletal clips and the companion files they came from, with skeletal exports.")},
	{"collision", "frame", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision boxes for map handoff, static or following the animation.")},
	{"mdl", "cube", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Quake MDL"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Quake MDL skins, groups, timing, header fields and the palette.")},
	{"export", "export", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Export"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Write the model for a game, stage it in a package, place it in the open map and build detail levels.")},
	{"health", "check", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Health"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Topology, intersections and export budgets; each finding leads to the elements concerned.")},
	{"view", "eye", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "View"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Shading, overlays, the view layout, navigation and the controls profile.")},
};

struct FamilyEntry {
	const char* family;
	const char* leading;
	const char* trailing;
	const char* groupStarts;
	const char* leadingCurrent;
	const char* trailingCurrent;
};

// Studio follows Blender's split: the outliner and what to add on one side,
// properties on the other.
constexpr FamilyEntry kFamilies[] = {
	{"studio", "outliner add", "item tool surface animation skeleton collision mdl export health view", "add animation export view",
		"outliner", "item"},
	// Blender: the Outliner beside the 3D view's Item, Tool and View tabs,
	// then the Properties editor's data, material, armature and physics tabs.
	{"blender", "outliner add", "item tool view surface skeleton animation collision mdl export health", "add surface mdl",
		"outliner", "item"},
	// 3ds Max: the Scene Explorer, and the Command Panel's Create, Modify,
	// Hierarchy, Motion, Display and Utilities.
	{"max", "outliner", "add item tool surface skeleton animation view export health collision mdl", "skeleton export",
		"outliner", "item"},
	// MilkShape 3D: one panel of Model, Groups, Materials and Joints tabs,
	// the animation controls below the views.
	{"milkshape", "", "tool outliner surface skeleton item animation collision mdl export health view add", "item collision",
		"", "tool"},
};

struct TitleEntry {
	const char* family;
	const char* tab;
	const char* title;
};

constexpr TitleEntry kTitles[] = {
	{"blender", "surface", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Material")},
	{"blender", "skeleton", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Armature")},
	{"blender", "collision", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Physics")},
	{"blender", "health", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Mesh Analysis")},
	{"max", "outliner", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Scene Explorer")},
	{"max", "add", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Create")},
	{"max", "item", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Modify")},
	{"max", "tool", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Tool Options")},
	{"max", "surface", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Surface Properties")},
	{"max", "skeleton", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Hierarchy")},
	{"max", "animation", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Motion")},
	{"max", "view", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Display")},
	{"max", "export", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Utilities")},
	{"max", "health", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "xView")},
	{"milkshape", "tool", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Model")},
	{"milkshape", "outliner", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Groups")},
	{"milkshape", "skeleton", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Joints")},
	{"milkshape", "item", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Transform")},
	{"milkshape", "surface", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Materials")},
	{"milkshape", "add", QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Primitives")},
};

struct ModeNames {
	const char* family;
	const char* names[6];
};

constexpr ModeNames kModeNames[] = {
	{"studio", {QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Faces"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Vertices"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Edges"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Tags"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Surfaces")}},
	{"blender", {QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Face"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Vertex"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Edge"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Tags and Bones"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Object")}},
	{"max", {QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Polygon"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Vertex"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Edge"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Helpers"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Object")}},
	{"milkshape", {QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Face"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Vertex"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Edge"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Joint"),
		QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Collision"), QT_TRANSLATE_NOOP("VibeStudioModelSidebar", "Group")}},
};

const FamilyEntry& familyEntry(const QString& family)
{
	for (const FamilyEntry& entry : kFamilies) {
		if (family == QLatin1String(entry.family)) {
			return entry;
		}
	}
	return kFamilies[0];
}

QStringList words(const char* text)
{
	return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

} // namespace

QVector<ModelSidebarTab> modelSidebarTabs()
{
	QVector<ModelSidebarTab> tabs;
	for (const TabEntry& entry : kTabs) {
		tabs.append({QString::fromLatin1(entry.id), QString::fromLatin1(entry.icon),
			QCoreApplication::translate("VibeStudioModelSidebar", entry.title),
			QCoreApplication::translate("VibeStudioModelSidebar", entry.description)});
	}
	return tabs;
}

QStringList modelSidebarTabIds()
{
	QStringList ids;
	for (const TabEntry& entry : kTabs) {
		ids << QString::fromLatin1(entry.id);
	}
	return ids;
}

bool modelSidebarTabForId(const QString& id, ModelSidebarTab* out)
{
	for (const TabEntry& entry : kTabs) {
		if (id == QLatin1String(entry.id)) {
			if (out) {
				*out = {QString::fromLatin1(entry.id), QString::fromLatin1(entry.icon),
					QCoreApplication::translate("VibeStudioModelSidebar", entry.title),
					QCoreApplication::translate("VibeStudioModelSidebar", entry.description)};
			}
			return true;
		}
	}
	return false;
}

QStringList modelSidebarFamilies()
{
	QStringList families;
	for (const FamilyEntry& entry : kFamilies) {
		families << QString::fromLatin1(entry.family);
	}
	return families;
}

ModelSidebarArrangement modelSidebarArrangementForFamily(const QString& family)
{
	const FamilyEntry& entry = familyEntry(family);
	ModelSidebarArrangement arrangement;
	arrangement.leading = words(entry.leading);
	arrangement.trailing = words(entry.trailing);
	arrangement.groupStarts = words(entry.groupStarts);
	arrangement.leadingCurrent = QString::fromLatin1(entry.leadingCurrent);
	arrangement.trailingCurrent = QString::fromLatin1(entry.trailingCurrent);
	return arrangement;
}

QString modelSidebarTabTitle(const QString& tabId, const QString& family)
{
	for (const TitleEntry& entry : kTitles) {
		if (family == QLatin1String(entry.family) && tabId == QLatin1String(entry.tab)) {
			return QCoreApplication::translate("VibeStudioModelSidebar", entry.title);
		}
	}
	ModelSidebarTab tab;
	return modelSidebarTabForId(tabId, &tab) ? tab.title : tabId;
}

QStringList modelSelectionModeNames(const QString& family)
{
	const ModeNames* chosen = &kModeNames[0];
	for (const ModeNames& entry : kModeNames) {
		if (family == QLatin1String(entry.family)) {
			chosen = &entry;
		}
	}
	QStringList names;
	for (const char* name : chosen->names) {
		names << QCoreApplication::translate("VibeStudioModelSidebar", name);
	}
	return names;
}

ModelSidebarArrangement normalizedModelSidebarArrangement(const ModelSidebarArrangement& arrangement, const QString& family)
{
	const QStringList known = modelSidebarTabIds();
	const ModelSidebarArrangement defaults = modelSidebarArrangementForFamily(family);
	ModelSidebarArrangement result;
	QSet<QString> placed;
	const auto place = [&](const QStringList& source, QStringList* target) {
		for (const QString& id : source) {
			if (known.contains(id) && !placed.contains(id)) {
				placed.insert(id);
				target->append(id);
			}
		}
	};
	place(arrangement.leading, &result.leading);
	place(arrangement.trailing, &result.trailing);
	for (const QString& id : known) {
		if (placed.contains(id)) {
			continue;
		}
		placed.insert(id);
		(defaults.leading.contains(id) ? result.leading : result.trailing).append(id);
	}
	for (const QString& id : arrangement.groupStarts) {
		if (known.contains(id) && !result.groupStarts.contains(id)) {
			result.groupStarts.append(id);
		}
	}
	result.leadingCurrent = result.leading.contains(arrangement.leadingCurrent) ? arrangement.leadingCurrent
		: result.leading.isEmpty() ? QString() : result.leading.first();
	result.trailingCurrent = result.trailing.contains(arrangement.trailingCurrent) ? arrangement.trailingCurrent
		: result.trailing.isEmpty() ? QString() : result.trailing.first();
	return result;
}

bool validateModelSidebarArrangement(const ModelSidebarArrangement& arrangement, QString* error)
{
	const QStringList known = modelSidebarTabIds();
	QSet<QString> seen;
	for (const QString& id : arrangement.leading + arrangement.trailing) {
		if (!known.contains(id)) {
			if (error) { *error = QCoreApplication::translate("VibeStudioModelSidebar", "Unknown sidebar tab: %1.").arg(id); }
			return false;
		}
		if (seen.contains(id)) {
			if (error) { *error = QCoreApplication::translate("VibeStudioModelSidebar", "The sidebar tab %1 appears twice.").arg(id); }
			return false;
		}
		seen.insert(id);
	}
	for (const QString& id : known) {
		if (!seen.contains(id)) {
			if (error) { *error = QCoreApplication::translate("VibeStudioModelSidebar", "The sidebar tab %1 is missing.").arg(id); }
			return false;
		}
	}
	return true;
}

QJsonObject modelSidebarArrangementJson(const ModelSidebarArrangement& arrangement, const QString& family)
{
	const auto side = [&family](const QStringList& ids) {
		QJsonArray array;
		for (const QString& id : ids) {
			QJsonObject tab;
			tab.insert(QStringLiteral("id"), id);
			tab.insert(QStringLiteral("title"), modelSidebarTabTitle(id, family));
			array.append(tab);
		}
		return array;
	};
	QJsonObject object;
	object.insert(QStringLiteral("family"), family);
	object.insert(QStringLiteral("leading"), side(arrangement.leading));
	object.insert(QStringLiteral("trailing"), side(arrangement.trailing));
	object.insert(QStringLiteral("groupStarts"), QJsonArray::fromStringList(arrangement.groupStarts));
	object.insert(QStringLiteral("leadingCurrent"), arrangement.leadingCurrent);
	object.insert(QStringLiteral("trailingCurrent"), arrangement.trailingCurrent);
	return object;
}

bool modelSidebarArrangementFromJson(const QJsonObject& object, ModelSidebarArrangement* out, QString* error)
{
	const auto ids = [](const QJsonValue& value) {
		QStringList list;
		for (const QJsonValue& entry : value.toArray()) {
			list << (entry.isObject() ? entry.toObject().value(QStringLiteral("id")).toString() : entry.toString());
		}
		return list;
	};
	if (!object.value(QStringLiteral("leading")).isArray() || !object.value(QStringLiteral("trailing")).isArray()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelSidebar", "A sidebar arrangement needs leading and trailing lists."); }
		return false;
	}
	ModelSidebarArrangement arrangement;
	arrangement.leading = ids(object.value(QStringLiteral("leading")));
	arrangement.trailing = ids(object.value(QStringLiteral("trailing")));
	for (const QJsonValue& value : object.value(QStringLiteral("groupStarts")).toArray()) {
		arrangement.groupStarts << value.toString();
	}
	arrangement.leadingCurrent = object.value(QStringLiteral("leadingCurrent")).toString();
	arrangement.trailingCurrent = object.value(QStringLiteral("trailingCurrent")).toString();
	*out = arrangement;
	return true;
}

} // namespace vibestudio
