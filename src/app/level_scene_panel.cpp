#include "app/level_scene_panel.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio {
LevelScenePanel::LevelScenePanel(LevelMapDocument* document, QWidget* parent) : QWidget(parent), m_document(document) {
	setObjectName(QStringLiteral("levelScenePanel"));
	setAccessibleName(tr("Level scene organization"));
	setAccessibleDescription(tr("Layers and nested groups with undoable membership, editor visibility and editing locks."));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	m_tree = new QTreeWidget(this);
	m_tree->setObjectName(QStringLiteral("levelSceneTree"));
	m_tree->setHeaderLabels({tr("Scene"), tr("Kind"), tr("Objects")});
	m_tree->setAccessibleName(tr("Layers and groups"));
	m_tree->setAccessibleDescription(
		tr("Select a node to manage it. The check box controls editor visibility; hidden content remains in builds and packages."));
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setUniformRowHeights(true);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	layout->addWidget(m_tree, 1);
	auto* select = new QPushButton(tr("Select members"), this);
	select->setObjectName(QStringLiteral("levelSceneSelect"));
	select->setToolTip(tr("Select visible objects in this node and all its nested groups."));
	layout->addWidget(select);
	connect(select, &QPushButton::clicked, this, [this] {
		if (!current()) {
			refresh();
			return;
		}
		QString error;
		if (!setLevelMapSelection(m_document, levelSceneSelection(*m_document, currentId()), &error)) {
			m_status->setText(error);
			return;
		}
		Q_EMIT selectionChanged();
	});
	m_editControls = new QWidget(this);
	auto* form = new QFormLayout(m_editControls);
	form->setContentsMargins(0, 0, 0, 0);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_name = new QLineEdit(this);
	m_name->setObjectName(QStringLiteral("levelSceneName"));
	m_name->setMaxLength(128);
	m_name->setAccessibleName(tr("Scene node name"));
	form->addRow(tr("Name"), m_name);
	m_parent = new QComboBox(this);
	m_parent->setObjectName(QStringLiteral("levelSceneParent"));
	m_parent->setAccessibleName(tr("Parent layer or group"));
	m_parent->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_parent->setMinimumContentsLength(10);
	form->addRow(tr("Parent"), m_parent);
	m_lock = new QCheckBox(tr("Lock editing"), this);
	m_lock->setObjectName(QStringLiteral("levelSceneLock"));
	m_lock->setAccessibleName(tr("Lock scene node editing"));
	m_lock->setToolTip(tr("Protect members and nested groups from changes. Inspection, copying, builds and packages remain available."));
	form->addRow(m_lock);
	connect(m_lock, &QCheckBox::clicked, this, [this](bool locked) {
		edit([&](QString* error) { return setLevelSceneLocked(m_document, currentId(), locked, error); });
	});
	auto* actions = new QWidget(this);
	auto* grid = new QGridLayout(actions);
	grid->setContentsMargins(0, 0, 0, 0);
	const auto button = [&](const QString& label, const char* name, int row, int column) {
		auto* item = new QPushButton(label, actions);
		item->setObjectName(QString::fromLatin1(name));
		item->setAccessibleName(label);
		grid->addWidget(item, row, column);
		return item;
	};
	auto* layer = button(tr("New layer"), "levelSceneNewLayer", 0, 0);
	auto* group = button(tr("New group"), "levelSceneNewGroup", 0, 1);
	m_rename = button(tr("Rename"), "levelSceneRename", 1, 0);
	m_move = button(tr("Move to parent"), "levelSceneMove", 1, 1);
	m_assign = button(tr("Assign selection"), "levelSceneAssign", 2, 0);
	m_remove = button(tr("Remove node"), "levelSceneRemove", 2, 1);
	m_remove->setToolTip(tr("Move members and child groups to the parent. Undo restores the node."));
	m_linkCopy = button(tr("Linked copy"), "levelSceneLinkCopy", 3, 0);
	m_linkCopy->setToolTip(tr("Copy this group beside itself and link the copies: an edit inside one is made to every copy, "
							  "while moving a whole copy moves only that one."));
	m_unlink = button(tr("Unlink"), "levelSceneUnlink", 3, 1);
	m_unlink->setToolTip(tr("Separate this copy from the others; it keeps its content."));
	m_updateLinks = button(tr("Update copies"), "levelSceneUpdateLinks", 4, 0);
	m_updateLinks->setToolTip(tr("Make every other unlocked copy match this one now, for copies changed in different ways at once."));
	m_selectLinks = button(tr("Select copies"), "levelSceneSelectLinks", 4, 1);
	m_selectLinks->setToolTip(tr("Select the visible members of every copy linked with this group."));
	form->addRow(actions);
	m_creation = new QComboBox(this);
	m_creation->setObjectName(QStringLiteral("levelSceneCreation"));
	m_creation->setAccessibleName(tr("Destination for new objects"));
	m_creation->setToolTip(
		tr("New primitives, entities and prefab placements join this node. Duplicates and clip results inherit their source membership."));
	m_creation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_creation->setMinimumContentsLength(10);
	form->addRow(tr("Create in"), m_creation);
	layout->addWidget(m_editControls);
	m_reset = new QPushButton(tr("Reset scene organization"), this);
	m_reset->setObjectName(QStringLiteral("levelSceneReset"));
	m_reset->setToolTip(tr("Discard unrecognized scene metadata. Geometry is retained; Undo restores the metadata."));
	layout->addWidget(m_reset);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("levelSceneStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(tr("Scene status"));
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	connect(layer, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) {
			return createLevelSceneNode(m_document, LevelSceneNodeKind::Layer, m_name->text(), {}, nullptr, error);
		});
	});
	connect(group, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) {
			return createLevelSceneNode(m_document, LevelSceneNodeKind::Group, m_name->text(), m_parent->currentData().toString(), nullptr,
										error);
		});
	});
	connect(m_rename, &QPushButton::clicked, this,
			[this] { edit([&](QString* error) { return renameLevelSceneNode(m_document, currentId(), m_name->text(), error); }); });
	connect(m_move, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) { return reparentLevelSceneNode(m_document, currentId(), m_parent->currentData().toString(), error); });
	});
	connect(m_remove, &QPushButton::clicked, this,
			[this] { edit([&](QString* error) { return removeLevelSceneNode(m_document, currentId(), error); }); });
	connect(m_assign, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) {
			QStringList members;
			for (const auto& ref : m_document->selection) {
				members << levelMapSelectionRefId(ref);
			}
			return assignLevelSceneObjects(m_document, currentId(), members, error);
		});
	});
	connect(m_reset, &QPushButton::clicked, this, [this] { edit([&](QString* error) { return resetLevelScene(m_document, error); }); });
	connect(m_linkCopy, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) {
			const QString id = currentId();
			return createLinkedLevelGroup(m_document, id, levelLinkedCopyOffset(*m_document, id), nullptr, error);
		});
	});
	connect(m_unlink, &QPushButton::clicked, this, [this] { edit([&](QString* error) { return unlinkLevelGroup(m_document, currentId(), error); }); });
	connect(m_updateLinks, &QPushButton::clicked, this, [this] {
		edit([&](QString* error) { return updateLinkedLevelGroups(m_document, currentId(), nullptr, error); });
	});
	connect(m_selectLinks, &QPushButton::clicked, this, [this] {
		if (!current()) {
			refresh();
			return;
		}
		QVector<LevelMapSelectionRef> members;
		for (const QString& id : levelLinkedGroupNodes(m_document->scene, currentId())) {
			members += levelSceneSelection(*m_document, id);
		}
		QString error;
		if (!setLevelMapSelection(m_document, members, &error)) {
			m_status->setText(error);
			return;
		}
		Q_EMIT selectionChanged();
	});
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { updateControls(); });
	connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (m_refreshing || column != 0 || item->data(0, Qt::UserRole).toString().isEmpty()) {
			return;
		}
		const auto id = item->data(0, Qt::UserRole).toString();
		const bool visible = item->checkState(0) == Qt::Checked;
		const auto revision = m_revision;
		const auto source = m_source;
		const auto hash = m_hash;
		const auto map = m_mapName;
		// QTreeWidgetItem::setData still uses the item after emitting this signal.
		// Rebuilding the tree here would delete that live item. Defer the entire
		// transaction and bind it to the displayed document revision.
		QTimer::singleShot(0, this, [this, id, visible, revision, source, hash, map] {
			if (!m_document || m_document->revision != revision || m_document->sourcePath != source ||
				m_document->sourceContentHash != hash || m_document->mapName != map) {
				refresh();
				m_status->setText(tr("The map changed. Review the current scene and try again."));
				return;
			}
			edit([&](QString* error) { return setLevelSceneVisible(m_document, id, visible, error); });
		});
	});
	connect(m_creation, &QComboBox::currentIndexChanged, this, [this] {
		if (!m_refreshing && current()) {
			m_document->activeSceneNode = m_creation->currentData().toString();
		}
	});
	refresh();
}

QString LevelScenePanel::currentId() const {
	return m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
}
bool LevelScenePanel::current() const {
	return m_document && m_document->revision == m_revision && m_document->sourcePath == m_source && m_document->mapName == m_mapName &&
		   m_document->sourceContentHash == m_hash;
}
void LevelScenePanel::edit(const std::function<bool(QString*)>& operation) {
	if (!current()) {
		refresh();
		m_status->setText(tr("The map changed. Review the current scene and try again."));
		return;
	}
	QString error;
	if (!operation(&error)) {
		refresh();
		m_status->setText(error);
		return;
	}
	refresh();
	Q_EMIT sceneChanged();
}
void LevelScenePanel::updateControls() {
	if (m_refreshing || !m_document) {
		return;
	}
	const auto* node = levelSceneNode(m_document->scene, currentId());
	const auto locked = levelSceneLockedNodes(m_document->scene);
	const bool inherited = node && locked.contains(node->parentId);
	const QSignalBlocker lockSignals(m_lock);
	m_lock->setEnabled(node && !inherited);
	m_lock->setChecked(node && (node->locked || inherited));
	m_lock->setText(inherited ? tr("Locked by parent") : tr("Lock editing"));
	m_rename->setEnabled(node);
	m_move->setEnabled(node && node->kind == LevelSceneNodeKind::Group && !locked.contains(node->id));
	m_remove->setEnabled(node && !locked.contains(node->id));
	m_assign->setEnabled(!m_document->selection.isEmpty() && !locked.contains(currentId()));
	const bool linked = node && !node->linkId.isEmpty();
	const bool quake = m_document->format == LevelMapFormat::QuakeMap || m_document->format == LevelMapFormat::Quake3Map;
	const bool parent = node && std::any_of(m_document->scene.nodes.cbegin(), m_document->scene.nodes.cend(),
									[node](const LevelSceneNode& other) { return other.parentId == node->id; });
	m_linkCopy->setEnabled(quake && node && node->kind == LevelSceneNodeKind::Group && !node->objects.isEmpty() && !parent);
	m_unlink->setEnabled(linked);
	m_updateLinks->setEnabled(linked && !locked.contains(node->id));
	m_selectLinks->setEnabled(linked);
	if (node) {
		m_name->setText(node->name);
		m_parent->setCurrentIndex(m_parent->findData(node->parentId));
	}
}
void LevelScenePanel::refreshSelection() {
	if (m_document) {
		m_assign->setEnabled(!m_document->selection.isEmpty() && !levelSceneLockedNodes(m_document->scene).contains(currentId()));
	}
}
void LevelScenePanel::refresh() {
	const auto selected = currentId();
	QSet<QString> expanded;
	for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
		if ((*it)->isExpanded()) {
			expanded.insert((*it)->data(0, Qt::UserRole).toString());
		}
	}
	m_refreshing = true;
	m_tree->clear();
	m_parent->clear();
	m_creation->clear();
	const bool open = m_document && m_document->format != LevelMapFormat::Unknown;
	setEnabled(open);
	if (!open) {
		m_refreshing = false;
		return;
	}
	m_revision = m_document->revision;
	m_source = m_document->sourcePath;
	m_mapName = m_document->mapName;
	m_hash = m_document->sourceContentHash;
	const auto& state = m_document->scene;
	const bool preserved = !state.opaqueMetadata.isEmpty() || !state.problem.isEmpty();
	const bool editable = !preserved && (m_document->doomFormat != LevelMapDoomFormat::Udmf || m_document->doomUdmf);
	m_editControls->setEnabled(editable);
	m_reset->setVisible(preserved);
	m_status->setText(state.problem);
	m_parent->addItem(tr("Default"), QString());
	m_creation->addItem(tr("Default"), QString());
	auto* implicit =
		new QTreeWidgetItem(m_tree, {tr("Default"), tr("Layer"), QString::number(levelSceneMembers(*m_document, {}, false).size())});
	implicit->setData(0, Qt::UserRole, QString());
	QHash<QString, QTreeWidgetItem*> items{{QString(), implicit}};
	const auto lockedNodes = levelSceneLockedNodes(state);
	for (const auto& node : state.nodes) {
		bool shown = node.visible;
		const auto* owner = levelSceneNode(state, node.parentId);
		for (int depth = 0; owner && depth < kLevelSceneMaxDepth; ++depth) {
			shown &= owner->visible;
			owner = levelSceneNode(state, owner->parentId);
		}
		auto kind = node.kind == LevelSceneNodeKind::Layer ? tr("Layer") : tr("Group");
		if (!shown) {
			kind += tr(" · Hidden");
		}
		if (lockedNodes.contains(node.id)) {
			kind += tr(" · Locked");
		}
		if (!node.linkId.isEmpty()) {
			kind += tr(" · Linked");
		}
		auto* item = new QTreeWidgetItem({node.name, kind, QString::number(node.objects.size())});
		item->setData(0, Qt::UserRole, node.id);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (editable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
		item->setCheckState(0, node.visible ? Qt::Checked : Qt::Unchecked);
		item->setToolTip(0, tr("%1\nID: %2\nDirect members: %3").arg(node.name, node.id).arg(node.objects.size())
								+ (node.linkId.isEmpty() ? QString()
														 : QLatin1Char('\n')
											 + tr("Linked with %n other copies", nullptr,
												 static_cast<int>(levelLinkedGroupNodes(state, node.id).size()) - 1)
											 + (node.linkTurn != 0 ? QLatin1Char('\n') + tr("Turned %1 degrees").arg(node.linkTurn * 90) : QString())
											 + (node.linkMirror ? QLatin1Char('\n') + tr("Mirrored") : QString())));
		item->setData(0, Qt::AccessibleDescriptionRole, tr("%1; %2; Direct members: %3").arg(node.name, kind).arg(node.objects.size()));
		items.insert(node.id, item);
		QStringList path{node.name};
		const auto* ancestor = levelSceneNode(state, node.parentId);
		for (int depth = 0; ancestor && depth < kLevelSceneMaxDepth; ++depth) {
			path.prepend(ancestor->name);
			ancestor = levelSceneNode(state, ancestor->parentId);
		}
		const auto label = path.join(QStringLiteral(" / ")) + (lockedNodes.contains(node.id) ? tr(" · Locked") : QString());
		m_parent->addItem(label, node.id);
		m_creation->addItem(label, node.id);
	}
	for (const auto& node : state.nodes) {
		auto* item = items.value(node.id);
		if (node.kind == LevelSceneNodeKind::Layer) {
			m_tree->addTopLevelItem(item);
		} else {
			items.value(node.parentId, implicit)->addChild(item);
		}
		item->setExpanded(expanded.contains(node.id));
	}
	implicit->setExpanded(true);
	m_tree->setCurrentItem(items.value(selected, implicit));
	m_creation->setCurrentIndex(std::max(0, m_creation->findData(m_document->activeSceneNode)));
	m_document->activeSceneNode = m_creation->currentData().toString();
	m_refreshing = false;
	updateControls();
}
} // namespace vibestudio
