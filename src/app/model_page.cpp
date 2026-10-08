// The Models page laid out like the Levels page: the package's models on a
// leading sidebar, the summary, skin, metadata and skeleton of the selected
// model on a trailing one, the preview between them navigating like the
// modeller under the chosen controls profile.
#include "app/application_shell.h"

#include "app/model_viewport.h"
#include "app/ui_primitives.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "core/model_editor_controls.h"
#include "core/model_skeleton.h"

#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vibestudio {

void ApplicationShell::buildModelSidebars(QSplitter* workbench, QWidget* browser, QWidget* centre, QWidget* appearance)
{
	m_modelBrowserSidebar = new StudioSidebar(tr("Model browser"), StudioSidebar::TabEdge::Trailing);
	m_modelBrowserSidebar->setObjectName(QStringLiteral("modelBrowserSidebar"));
	auto* models = new SidebarPage(QStringLiteral("models"), QStringLiteral("cube"), tr("Models"),
		tr("Model files in the open package. Filter by name or by key=value, such as ext=md5mesh."));
	models->setObjectName(QStringLiteral("modelBrowserPage"));
	models->addWidget(browser, 1);
	m_modelBrowserSidebar->addPage(models);

	m_modelInspectorSidebar = new StudioSidebar(tr("Model inspector"), StudioSidebar::TabEdge::Leading);
	m_modelInspectorSidebar->setObjectName(QStringLiteral("modelInspector"));
	const auto page = [this](const QString& id, const QString& icon, const QString& title, const QString& description, QWidget* body,
						  bool groupStart = false) {
		auto* sidebarPage = new SidebarPage(id, icon, title, description);
		sidebarPage->setObjectName(QStringLiteral("modelInspector-") + id);
		sidebarPage->addWidget(body, 1);
		m_modelInspectorSidebar->addPage(sidebarPage, groupStart);
		return sidebarPage;
	};
	page(QStringLiteral("summary"), QStringLiteral("info"), tr("Summary"),
		tr("Frames, surfaces, tags, vertex and triangle counts, animations, and skin paths."), m_modelDetails);
	page(QStringLiteral("skin"), QStringLiteral("image"), tr("Skin"), tr("The surface materials and skins the model uses, and how they resolve."), appearance);
	page(QStringLiteral("metadata"), QStringLiteral("properties"), tr("Metadata"), tr("Header fields, skin and material dependencies, and raw metadata."),
		m_modelDrawer);
	m_modelSkeletonTree = new QTreeWidget;
	m_modelSkeletonTree->setObjectName(QStringLiteral("modelSkeleton"));
	m_modelSkeletonTree->setAccessibleName(tr("Model joints"));
	m_modelSkeletonTree->setAccessibleDescription(tr("The joint hierarchy and skeletal clips of skeletal models such as MD5, MDS, MDM, Ghoul 2 and IQM."));
	m_modelSkeletonTree->setHeaderLabels({tr("Joint or clip"), tr("Detail")});
	m_modelSkeletonTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_modelSkeletonTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_modelSkeletonTree->header()->setStretchLastSection(false);
	page(QStringLiteral("skeleton"), QStringLiteral("link"), tr("Skeleton"),
		tr("Joints, skeletal clips and the companion files read with the model (md5anim, MDX, GLA)."), m_modelSkeletonTree, true);

	workbench->addWidget(m_modelBrowserSidebar);
	workbench->addWidget(centre);
	workbench->addWidget(m_modelInspectorSidebar);
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setStretchFactor(2, 0);
	workbench->setSizes({260, 720, 330});
	applyModelViewportProfile();
	refreshModelSkeletonPanel();
}

void ApplicationShell::applyModelViewportProfile()
{
	if (!m_modelViewport) {
		return;
	}
	const QString profileId = m_settings.modelEditorProfileId();
	const ModelEditorControls controls = m_settings.effectiveModelEditorControls(profileId);
	auto camera = controls.navigation.view3D;
	camera.perspective = m_modelViewport->isPerspective();
	m_modelViewport->setCameraControls(camera, true);
	// Say how this profile moves the view, from the profile's own rows.
	QStringList navigation;
	const auto rows = modelEditorControlRows(controls);
	for (const auto& row : rows) {
		// The 3D view's orbit, pan and zoom come first in the rows.
		if (row.section == QLatin1String("navigation") && row.view == rows.value(0).view && navigation.size() < 3) {
			navigation << QStringLiteral("%1: %2").arg(row.action, row.gesture);
		}
	}
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(profileId, &profile));
	const QString help = tr("%1 controls. %2.").arg(profile.shortName, navigation.join(QStringLiteral("; ")));
	m_modelViewport->setControlsHelp(help);
	m_modelViewport->setAccessibleDescription(help);
	if (m_modelHover && (m_modelHover->text().isEmpty() || !m_modelViewport->hasMesh())) {
		m_modelHover->setText(help);
	}
}

void ApplicationShell::refreshModelSkeletonPanel()
{
	if (!m_modelSkeletonTree) {
		return;
	}
	m_modelSkeletonTree->clear();
	const ModelSkeleton& skeleton = m_modelMesh.skeleton;
	if (auto* page = m_modelInspectorSidebar ? m_modelInspectorSidebar->page(QStringLiteral("skeleton")) : nullptr) {
		page->setBadge(skeleton.isEmpty() ? QString() : QString::number(skeleton.joints.size()));
	}
	if (skeleton.isEmpty()) {
		auto* item = new QTreeWidgetItem(m_modelSkeletonTree);
		item->setText(0, m_modelMesh.geometryAvailable ? tr("No joints: this model animates by frames.") : tr("No model selected."));
		item->setFlags(Qt::ItemIsEnabled);
		return;
	}
	auto* joints = new QTreeWidgetItem(m_modelSkeletonTree);
	joints->setText(0, tr("Joints"));
	joints->setText(1, QString::number(skeleton.joints.size()));
	QVector<QTreeWidgetItem*> items(skeleton.joints.size(), nullptr);
	for (int index = 0; index < skeleton.joints.size(); ++index) {
		const ModelJoint& joint = skeleton.joints.at(index);
		auto* parent = joint.parent >= 0 && joint.parent < index && items.at(joint.parent) ? items.at(joint.parent) : joints;
		auto* item = new QTreeWidgetItem(parent);
		item->setText(0, joint.name);
		const ModelVec3 origin = modelJointTranslation(joint.bind);
		item->setText(1, QStringLiteral("%1, %2, %3").arg(origin.x, 0, 'g', 5).arg(origin.y, 0, 'g', 5).arg(origin.z, 0, 'g', 5));
		items[index] = item;
	}
	auto* clips = new QTreeWidgetItem(m_modelSkeletonTree);
	clips->setText(0, tr("Clips"));
	clips->setText(1, QString::number(skeleton.clips.size()));
	for (const ModelSkeletalClip& clip : skeleton.clips) {
		auto* item = new QTreeWidgetItem(clips);
		item->setText(0, clip.name);
		item->setText(1, clip.framesPerSecond > 0 ? tr("%1 frames at %2 fps").arg(clip.frames.size()).arg(clip.framesPerSecond)
												  : tr("%1 frames").arg(clip.frames.size()));
		item->setToolTip(0, clip.sourcePath);
	}
	if (!m_modelMesh.companionPaths.isEmpty()) {
		auto* companions = new QTreeWidgetItem(m_modelSkeletonTree);
		companions->setText(0, tr("Read with"));
		companions->setText(1, QString::number(m_modelMesh.companionPaths.size()));
		for (const QString& path : m_modelMesh.companionPaths) {
			auto* item = new QTreeWidgetItem(companions);
			item->setText(0, path);
		}
	}
	joints->setExpanded(true);
	clips->setExpanded(true);
}

} // namespace vibestudio
