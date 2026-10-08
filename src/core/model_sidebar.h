#pragma once

// The modeller's two tabbed sidebars, as data, in the manner of the Levels
// page (core/level_sidebar.h).
//
// Every panel beside the modeller's views lives on a sidebar tab: the
// outliner, primitives, the item and tool settings, surfaces and materials,
// animation, the skeleton, collision, Quake MDL settings, export, health and
// view options. Which tabs sit on the leading sidebar and which on the
// trailing one, their order, where groups break and what each is called follow
// the modeller profile's family, so a Blender user finds an Outliner, Item and
// Tool tabs; a 3ds Max user a Scene Explorer and a Command Panel of Create,
// Modify, Hierarchy, Motion, Display and Utilities; and a MilkShape 3D user
// Model, Groups, Materials and Joints.
//
// "Leading" and "trailing" are reading-order sides: leading is the left side
// in a left-to-right layout and the right side in a right-to-left one.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct ModelSidebarTab {
	QString id;
	QString iconName;
	// The neutral title and what the tab is for, translated.
	QString title;
	QString description;
};

struct ModelSidebarArrangement {
	QStringList leading;
	QStringList trailing;
	// Tabs that start a new group: a gap and a rule set them apart.
	QStringList groupStarts;
	QString leadingCurrent;
	QString trailingCurrent;
};

// Every tab, in the studio's own order.
[[nodiscard]] QVector<ModelSidebarTab> modelSidebarTabs();
[[nodiscard]] QStringList modelSidebarTabIds();
[[nodiscard]] bool modelSidebarTabForId(const QString& id, ModelSidebarTab* out = nullptr);

// "studio", "blender", "max" or "milkshape". Unknown families take "studio".
[[nodiscard]] QStringList modelSidebarFamilies();
[[nodiscard]] ModelSidebarArrangement modelSidebarArrangementForFamily(const QString& family);
// What a tab is called in a family, translated.
[[nodiscard]] QString modelSidebarTabTitle(const QString& tabId, const QString& family);
// The selection-mode names a family uses, translated, in the editor's mode
// order: faces, vertices, edges, tags, collision, surfaces.
[[nodiscard]] QStringList modelSelectionModeNames(const QString& family);

// Puts every known tab on exactly one side once: unknown and repeated ids go,
// missing tabs join the side the family gives them, and the current tabs fall
// back to the first of their side.
[[nodiscard]] ModelSidebarArrangement normalizedModelSidebarArrangement(const ModelSidebarArrangement& arrangement, const QString& family);
[[nodiscard]] bool validateModelSidebarArrangement(const ModelSidebarArrangement& arrangement, QString* error = nullptr);
[[nodiscard]] QJsonObject modelSidebarArrangementJson(const ModelSidebarArrangement& arrangement, const QString& family);
[[nodiscard]] bool modelSidebarArrangementFromJson(const QJsonObject& object, ModelSidebarArrangement* out, QString* error = nullptr);

} // namespace vibestudio
