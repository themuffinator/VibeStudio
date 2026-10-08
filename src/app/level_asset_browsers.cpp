// The Levels sidebar's Models, Sounds and Prefabs tabs: browse what the open
// package and project hold, look at or listen to it, then drag it onto a view,
// place it in the middle of one, or give it to the selected entities. After
// VibeRadiant's asset browser tabs (see docs/CREDITS.md).

#include "app/application_shell.h"

#include "app/asset_views.h"
#include "app/audio_browser_worker.h"
#include "app/audio_playback.h"
#include "app/level_palette_tree.h"
#include "app/map_viewport.h"
#include "app/model_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "core/asset_tools.h"
#include "core/level_materials.h"

#include <QDir>
#include <QEvent>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace vibestudio {

namespace {

constexpr int kEntryIndexRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;

QLineEdit* browserFilter(const QString& placeholder, const QString& accessibleName)
{
	auto* field = new QLineEdit;
	field->setPlaceholderText(placeholder);
	field->setAccessibleName(accessibleName);
	field->setClearButtonEnabled(true);
	field->addAction(studioIcon(QStringLiteral("filter"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	clearOnEscape(field);
	return field;
}

// An empty browser says why in wrapped text over its list, where a row
// would be cut short at the sidebar's width. Rows arriving hide it.
class BrowserPlaceholder final : public QLabel {
public:
	explicit BrowserPlaceholder(QAbstractItemView* view)
		: QLabel(view->viewport())
	{
		setObjectName(QStringLiteral("browserPlaceholder"));
		setProperty("sidebarHint", true);
		setWordWrap(true);
		setTextFormat(Qt::PlainText);
		setAlignment(Qt::AlignLeading | Qt::AlignTop);
		setContentsMargins(8, 8, 8, 8);
		hide();
		view->viewport()->installEventFilter(this);
		connect(view->model(), &QAbstractItemModel::rowsInserted, this, &QWidget::hide);
	}

	void showText(const QString& text)
	{
		setText(text);
		fit();
		show();
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget() && event->type() == QEvent::Resize) {
			fit();
		}
		return QLabel::eventFilter(watched, event);
	}

private:
	void fit()
	{
		const int width = parentWidget()->width();
		setGeometry(0, 0, width, heightForWidth(width));
	}
};

MapPaletteTree* browserTree(const QString& objectName, const QString& accessibleName, const QString& description)
{
	auto* tree = new MapPaletteTree;
	tree->setObjectName(objectName);
	tree->setAccessibleName(accessibleName);
	tree->setAccessibleDescription(description);
	tree->setHeaderHidden(true);
	tree->setRootIsDecorated(true);
	tree->setUniformRowHeights(true);
	tree->setDragEnabled(true);
	tree->setDragDropMode(QAbstractItemView::DragOnly);
	tree->setIndentation(14);
	setBaseIconSize(tree, QSize(16, 16));
	new BrowserPlaceholder(tree);
	return tree;
}

QWidget* filteredBrowser(QLineEdit* filter, QWidget* view)
{
	auto* panel = new QWidget;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(filter);
	layout->addWidget(view, 1);
	return panel;
}

QLabel* browserHint(const QString& objectName)
{
	auto* label = new QLabel;
	label->setObjectName(objectName);
	label->setProperty("sidebarHint", true);
	label->setWordWrap(true);
	label->setTextFormat(Qt::PlainText);
	return label;
}

// A hint with nothing to say takes no room.
void showHint(QLabel* label, const QString& text)
{
	label->setText(text);
	label->setVisible(!text.isEmpty());
}

// Keeps a row whose text or path holds the filter, and the folders above it.
bool filterBrowserItem(QTreeWidgetItem* item, const QString& needle)
{
	bool childMatches = false;
	for (int index = 0; index < item->childCount(); ++index) {
		childMatches = filterBrowserItem(item->child(index), needle) || childMatches;
	}
	const bool matches = needle.isEmpty() || item->text(0).toCaseFolded().contains(needle)
		|| item->data(0, kPathRole).toString().toCaseFolded().contains(needle);
	item->setHidden(!(matches || childMatches));
	if (!needle.isEmpty() && childMatches) {
		item->setExpanded(true);
	}
	return matches || childMatches;
}

void filterBrowser(QTreeWidget* tree, const QLineEdit* filter)
{
	if (!tree || !filter) {
		return;
	}
	const QString needle = filter->text().trimmed().toCaseFolded();
	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		filterBrowserItem(tree->topLevelItem(index), needle);
	}
}

QString currentPayload(const QTreeWidget* tree)
{
	const QTreeWidgetItem* item = tree ? tree->currentItem() : nullptr;
	return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void selectPayload(QTreeWidget* tree, const QString& payload)
{
	if (payload.isEmpty()) {
		return;
	}
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toString() == payload) {
			tree->setCurrentItem(*it);
			return;
		}
	}
}

void addNote(QTreeWidget* tree, const QString& text)
{
	if (auto* placeholder = static_cast<BrowserPlaceholder*>(
			tree->viewport()->findChild<QLabel*>(QStringLiteral("browserPlaceholder"), Qt::FindDirectChildrenOnly))) {
		placeholder->showText(text);
		return;
	}
	auto* note = new QTreeWidgetItem(tree, {text});
	note->setFlags(Qt::ItemIsEnabled);
	note->setToolTip(0, text);
}

// Files grouped under their folders, the folders in path order. Each file
// row carries "<prefix>:<path>" for dragging and the entry's row in the
// archive for readers that address entries by position.
int fillGroupedTree(QTreeWidget* tree, const QVector<QPair<QString, qsizetype>>& files, const QString& prefix, const QString& iconName)
{
	QHash<QString, QTreeWidgetItem*> folders;
	const QIcon folderIcon = studioIcon(QStringLiteral("folder"), StudioIconTone::Muted);
	const QIcon fileIcon = studioIcon(iconName, StudioIconTone::Muted);
	for (const auto& [path, index] : files) {
		const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
		const QString folder = slash >= 0 ? path.left(slash) : QString();
		const QString name = slash >= 0 ? path.mid(slash + 1) : path;
		QTreeWidgetItem* parent = nullptr;
		if (!folder.isEmpty()) {
			QTreeWidgetItem*& group = folders[folder];
			if (!group) {
				group = new QTreeWidgetItem(tree, {folder});
				group->setIcon(0, folderIcon);
				group->setFlags(Qt::ItemIsEnabled);
				group->setData(0, kPathRole, folder);
			}
			parent = group;
		}
		auto* item = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(tree, {name});
		item->setIcon(0, fileIcon);
		item->setData(0, Qt::UserRole, prefix + QLatin1Char(':') + path);
		item->setData(0, kPathRole, path);
		item->setData(0, kEntryIndexRole, static_cast<qlonglong>(index));
		item->setToolTip(0, path);
		item->setData(0, Qt::AccessibleTextRole, path);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
	}
	// Few folders open; many stay closed so the list starts short.
	const bool expand = folders.size() <= 6;
	for (QTreeWidgetItem* group : std::as_const(folders)) {
		group->setExpanded(expand);
	}
	return static_cast<int>(files.size());
}

QVector<QPair<QString, qsizetype>> archiveFilesOfKind(const PackageArchiveReader& archive, AssetPreviewKind kind)
{
	QVector<QPair<QString, qsizetype>> files;
	const auto entries = archive.entries();
	for (qsizetype index = 0; index < entries.size(); ++index) {
		const PackageEntry& entry = entries.at(index);
		if (entry.kind == PackageEntryKind::File && assetPreviewKindForEntry(entry.virtualPath, entry.typeHint) == kind) {
			files.push_back({entry.virtualPath, index});
		}
	}
	std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) { return left.first.compare(right.first, Qt::CaseInsensitive) < 0; });
	return files;
}

QString durationText(qint64 milliseconds)
{
	const qint64 seconds = std::max<qint64>(0, milliseconds) / 1000;
	return QStringLiteral("%1:%2.%3").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')).arg((std::max<qint64>(0, milliseconds) % 1000) / 100);
}

QPushButton* browserButton(const QString& objectName, const QString& text, const QString& icon, const QString& toolTip)
{
	auto* button = createButton(text, icon);
	button->setObjectName(objectName);
	button->setToolTip(toolTip);
	button->setAccessibleDescription(toolTip);
	return button;
}

} // namespace

void ApplicationShell::buildLevelModelBrowser(SidebarPage* page)
{
	m_levelModelFilter = browserFilter(tr("Filter models"), tr("Model filter"));
	m_levelModelFilter->setObjectName(QStringLiteral("levelModelFilter"));
	m_levelModelList = browserTree(QStringLiteral("levelModelList"), tr("Package models"),
		tr("The models in the open package, by folder. Drag one onto a view to place it there; Enter places it in the middle of the active view."));
	addLevelSidebarSection(page, QStringLiteral("models.browser"), tr("Models"), filteredBrowser(m_levelModelFilter, m_levelModelList), 1);

	auto* preview = new QWidget;
	auto* previewLayout = new QVBoxLayout(preview);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	previewLayout->setSpacing(6);
	m_levelModelPreview = new ModelViewport;
	m_levelModelPreview->setObjectName(QStringLiteral("levelModelPreview"));
	m_levelModelPreview->setAccessibleName(tr("Model preview"));
	m_levelModelPreview->setAccessibleDescription(tr("The chosen model, shaded with its skin. Drag to turn it; the wheel zooms."));
	m_levelModelPreview->setMinimumWidth(160);
	m_levelModelPreview->setFixedHeight(scaledIconSize(QSize(200, 200)).height());
	m_levelModelPreview->setRenderMode(ModelViewportRenderMode::Textured);
	previewLayout->addWidget(m_levelModelPreview, 1);
	m_levelModelInfo = browserHint(QStringLiteral("levelModelInfo"));
	showHint(m_levelModelInfo, QString());
	previewLayout->addWidget(m_levelModelInfo);
	addLevelSidebarSection(page, QStringLiteral("models.preview"), tr("Preview"), preview);

	auto* place = new QWidget;
	auto* placeLayout = new QVBoxLayout(place);
	placeLayout->setContentsMargins(0, 0, 0, 0);
	placeLayout->setSpacing(6);
	auto* buttons = new QVBoxLayout;
	buttons->setSpacing(6);
	auto* placeButton = browserButton(QStringLiteral("levelModelPlace"), tr("Place in View"), QStringLiteral("crosshair"),
		tr("Add a model entity for the chosen model in the middle of the active view."));
	auto* assignButton = browserButton(QStringLiteral("levelModelAssign"), tr("Give to Selection"), QStringLiteral("edit"),
		tr("Set the model key of the selected entities to the chosen model."));
	m_levelModelPlace = placeButton;
	m_levelModelAssign = assignButton;
	buttons->addWidget(placeButton);
	buttons->addWidget(assignButton);
	placeLayout->addLayout(buttons);
	m_levelModelPlaceHint = browserHint(QStringLiteral("levelModelPlaceHint"));
	placeLayout->addWidget(m_levelModelPlaceHint);
	addLevelSidebarSection(page, QStringLiteral("models.place"), tr("Place"), place);

	auto* reload = page->addHeaderButton(QStringLiteral("refresh"), tr("Reload Models"), tr("List the package's models again."));
	reload->setObjectName(QStringLiteral("levelModelReload"));
	connect(reload, &QToolButton::clicked, this, [this]() {
		m_levelModelListKey.clear();
		refreshLevelModelBrowser();
	});
	connect(m_levelModelFilter, &QLineEdit::textChanged, this, [this]() { filterBrowser(m_levelModelList, m_levelModelFilter); });
	connect(m_levelModelList, &QTreeWidget::currentItemChanged, this, [this]() { showLevelModelPreview(); });
	connect(m_levelModelList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (!payload.isEmpty()) {
			placeLevelAsset(payload, QPointF(-1.0, -1.0));
		}
	});
	connect(placeButton, &QPushButton::clicked, this, [this]() { placeLevelAsset(currentPayload(m_levelModelList), QPointF(-1.0, -1.0)); });
	connect(assignButton, &QPushButton::clicked, this, [this]() { assignLevelAsset(currentPayload(m_levelModelList)); });
	refreshLevelModelBrowser();
}

void ApplicationShell::buildLevelSoundBrowser(SidebarPage* page)
{
	m_levelSoundFilter = browserFilter(tr("Filter sounds"), tr("Sound filter"));
	m_levelSoundFilter->setObjectName(QStringLiteral("levelSoundFilter"));
	m_levelSoundList = browserTree(QStringLiteral("levelSoundList"), tr("Package sounds"),
		tr("The sounds in the open package, by folder. Space plays the chosen one; drag one onto a view to place a speaker there."));
	// Space listens, as on the Audio page; the list keeps its other keys.
	auto* listen = new QShortcut(QKeySequence(Qt::Key_Space), m_levelSoundList, nullptr, nullptr, Qt::WidgetShortcut);
	connect(listen, &QShortcut::activated, this, &ApplicationShell::toggleLevelSoundPlayback);
	addLevelSidebarSection(page, QStringLiteral("sounds.browser"), tr("Sounds"), filteredBrowser(m_levelSoundFilter, m_levelSoundList), 1);

	auto* preview = new QWidget;
	auto* previewLayout = new QVBoxLayout(preview);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	previewLayout->setSpacing(6);
	m_levelSoundWave = new WaveformView;
	m_levelSoundWave->setObjectName(QStringLiteral("levelSoundWave"));
	m_levelSoundWave->setAccessibleName(tr("Sound waveform"));
	m_levelSoundWave->setMinimumHeight(64);
	previewLayout->addWidget(m_levelSoundWave);
	auto* transport = new QVBoxLayout;
	transport->setSpacing(6);
	auto* play = browserButton(QStringLiteral("levelSoundPlay"), tr("Play"), QStringLiteral("play"), tr("Listen to the chosen sound (Space)."));
	m_levelSoundPlay = play;
	transport->addWidget(play);
	m_levelSoundInfo = browserHint(QStringLiteral("levelSoundInfo"));
	showHint(m_levelSoundInfo, QString());
	m_levelSoundWave->setEmptyText(tr("Choose a sound to see and hear it."));
	transport->addWidget(m_levelSoundInfo);
	previewLayout->addLayout(transport);
	addLevelSidebarSection(page, QStringLiteral("sounds.preview"), tr("Listen"), preview);

	auto* place = new QWidget;
	auto* placeLayout = new QVBoxLayout(place);
	placeLayout->setContentsMargins(0, 0, 0, 0);
	placeLayout->setSpacing(6);
	auto* buttons = new QVBoxLayout;
	buttons->setSpacing(6);
	auto* placeButton = browserButton(QStringLiteral("levelSoundPlace"), tr("Place in View"), QStringLiteral("crosshair"),
		tr("Add a speaker entity playing the chosen sound in the middle of the active view."));
	auto* assignButton = browserButton(QStringLiteral("levelSoundAssign"), tr("Give to Selection"), QStringLiteral("edit"),
		tr("Set the noise key of the selected entities to the chosen sound."));
	m_levelSoundPlace = placeButton;
	m_levelSoundAssign = assignButton;
	buttons->addWidget(placeButton);
	buttons->addWidget(assignButton);
	placeLayout->addLayout(buttons);
	m_levelSoundPlaceHint = browserHint(QStringLiteral("levelSoundPlaceHint"));
	placeLayout->addWidget(m_levelSoundPlaceHint);
	addLevelSidebarSection(page, QStringLiteral("sounds.place"), tr("Place"), place);

	auto* reload = page->addHeaderButton(QStringLiteral("refresh"), tr("Reload Sounds"), tr("List the package's sounds again."));
	reload->setObjectName(QStringLiteral("levelSoundReload"));
	connect(reload, &QToolButton::clicked, this, [this]() {
		m_levelSoundListKey.clear();
		refreshLevelSoundBrowser();
	});
	connect(m_levelSoundFilter, &QLineEdit::textChanged, this, [this]() { filterBrowser(m_levelSoundList, m_levelSoundFilter); });
	connect(m_levelSoundList, &QTreeWidget::currentItemChanged, this, [this]() {
		stopLevelSoundPlayback();
		showLevelSoundPreview();
	});
	connect(m_levelSoundList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (!payload.isEmpty()) {
			placeLevelAsset(payload, QPointF(-1.0, -1.0));
		}
	});
	connect(play, &QPushButton::clicked, this, &ApplicationShell::toggleLevelSoundPlayback);
	connect(placeButton, &QPushButton::clicked, this, [this]() { placeLevelAsset(currentPayload(m_levelSoundList), QPointF(-1.0, -1.0)); });
	connect(assignButton, &QPushButton::clicked, this, [this]() { assignLevelAsset(currentPayload(m_levelSoundList)); });

	m_levelSoundWorker = new AudioBrowserWorker(this);
	m_levelSoundWorker->completed = [this](const AudioBrowserResult& result) {
		if (result.revision != m_levelSoundRevision || result.virtualPath != m_levelSoundShownPath || !m_levelSoundWave) {
			return;
		}
		const PackagePreview& preview = result.preview;
		// Without peaks the waveform's own hint says the audio could not be decoded.
		m_levelSoundWave->setEmptyText(QString());
		if (!result.error.isEmpty()) {
			m_levelSoundWave->clearPeaks();
			showHint(m_levelSoundInfo, result.error);
			return;
		}
		if (!preview.audioPeaks.peaks.isEmpty()) {
			m_levelSoundWave->setPeaks(preview.audioPeaks.peaks, preview.audioPeaks.channels, preview.audioSampleRate, preview.audioDurationMs);
		} else {
			m_levelSoundWave->clearPeaks();
		}
		QStringList facts;
		if (preview.audioDurationMs > 0) {
			facts << durationText(preview.audioDurationMs);
		}
		if (preview.audioSampleRate > 0) {
			facts << tr("%1 Hz").arg(preview.audioSampleRate);
		}
		if (preview.audioChannels > 0) {
			facts << (preview.audioChannels == 1 ? tr("mono") : tr("%n channel(s)", nullptr, preview.audioChannels));
		}
		if (!preview.audioFormat.isEmpty()) {
			facts << preview.audioFormat;
		}
		showHint(m_levelSoundInfo, facts.join(QStringLiteral(" · ")));
	};
	m_levelSoundAuditionWorker = new AudioBrowserWorker(this);
	m_levelSoundAuditionWorker->completed = [this](const AudioBrowserResult& result) {
		if (result.revision != m_levelSoundRevision || result.virtualPath != m_levelSoundShownPath) {
			return;
		}
		if (!result.error.isEmpty() || !result.source.playable()) {
			statusBar()->showMessage(tr("Unable to play %1: %2").arg(result.virtualPath, result.error.isEmpty() ? tr("No playable audio was prepared.") : result.error));
			stopLevelSoundPlayback();
			return;
		}
		if (!m_levelSoundPlayback) {
			m_levelSoundPlayback = new AudioPlayback(this);
			m_levelSoundPlayback->setObjectName(QStringLiteral("levelSoundPlayback"));
			m_levelSoundPlayback->setVolume(0.8f);
			connect(m_levelSoundPlayback, &AudioPlayback::changed, this, [this]() {
				const bool playing = m_levelSoundPlayback->state() == AudioPlayback::State::Playing || m_levelSoundPlayback->state() == AudioPlayback::State::Loading;
				if (m_levelSoundPlay) {
					m_levelSoundPlay->setText(playing ? tr("Stop") : tr("Play"));
					static_cast<QPushButton*>(m_levelSoundPlay)->setIcon(studioIcon(playing ? QStringLiteral("stop") : QStringLiteral("play")));
				}
			});
			connect(m_levelSoundPlayback, &AudioPlayback::positionChanged, this, [this](qint64) {
				if (m_levelSoundWave && m_levelSoundPlayback) {
					m_levelSoundWave->setPlayhead(m_levelSoundPlayback->positionMilliseconds());
				}
			});
			connect(m_levelSoundPlayback, &AudioPlayback::failed, this, [this](const QString& message) {
				statusBar()->showMessage(tr("Unable to play %1: %2").arg(m_levelSoundShownPath, message));
			});
		}
		if (!m_levelSoundPlayback->available()) {
			statusBar()->showMessage(tr("This build has no audio playback; open the sound on the Audio page for its details."));
			return;
		}
		m_levelSoundPlayback->startMedia(result.source.bytes, result.source.fileName, m_levelSoundWave ? m_levelSoundWave->durationMs() : 0, 0);
		statusBar()->showMessage(tr("Playing %1").arg(result.virtualPath), 3000);
	};
	refreshLevelSoundBrowser();
}

void ApplicationShell::buildLevelPrefabBrowser(SidebarPage* page)
{
	m_levelPrefabFilter = browserFilter(tr("Filter prefabs"), tr("Prefab filter"));
	m_levelPrefabFilter->setObjectName(QStringLiteral("levelPrefabFilter"));
	m_levelPrefabList = browserTree(QStringLiteral("levelPrefabList"), tr("Prefabs"),
		tr("Prefabs saved in the project folder and in the open package. Enter or a double-click inserts one with a preview."));
	m_levelPrefabList->setDragEnabled(false);
	addLevelSidebarSection(page, QStringLiteral("prefabs.library"), tr("Library"), filteredBrowser(m_levelPrefabFilter, m_levelPrefabList), 1);

	auto* actions = new QWidget;
	auto* actionsLayout = new QVBoxLayout(actions);
	actionsLayout->setContentsMargins(0, 0, 0, 0);
	actionsLayout->setSpacing(6);
	auto* row = new QVBoxLayout;
	row->setSpacing(6);
	auto* insert = browserButton(QStringLiteral("levelPrefabInsert"), tr("Insert…"), QStringLiteral("plus"),
		tr("Preview, position and turn the chosen prefab, then insert it as one undo step."));
	auto* save = browserButton(QStringLiteral("levelPrefabSave"), tr("Save Selection…"), QStringLiteral("save"),
		tr("Save the selection as a prefab file, with its asset references and an anchor."));
	row->addWidget(insert);
	row->addWidget(save);
	actionsLayout->addLayout(row);
	m_levelPrefabInfo = browserHint(QStringLiteral("levelPrefabInfo"));
	actionsLayout->addWidget(m_levelPrefabInfo);
	addLevelSidebarSection(page, QStringLiteral("prefabs.actions"), tr("Use"), actions);

	auto* reload = page->addHeaderButton(QStringLiteral("refresh"), tr("Reload Prefabs"), tr("Look for prefabs in the project and package again."));
	reload->setObjectName(QStringLiteral("levelPrefabReload"));
	connect(reload, &QToolButton::clicked, this, [this]() {
		m_levelPrefabListKey.clear();
		refreshLevelPrefabBrowser();
	});
	const auto insertCurrent = [this]() {
		const QTreeWidgetItem* item = m_levelPrefabList ? m_levelPrefabList->currentItem() : nullptr;
		const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (payload.startsWith(QStringLiteral("prefab-file:"))) {
			showLevelPrefab(false, false, payload.mid(12));
		} else if (payload.startsWith(QStringLiteral("prefab-entry:"))) {
			showLevelPrefab(false, false, payload.mid(13), true);
		} else {
			showLevelPrefab(false);
		}
	};
	connect(insert, &QPushButton::clicked, this, insertCurrent);
	connect(m_levelPrefabList, &QTreeWidget::itemActivated, this, [insertCurrent](QTreeWidgetItem* item) {
		if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) {
			insertCurrent();
		}
	});
	connect(save, &QPushButton::clicked, this, [this]() { showLevelPrefab(true); });
	connect(m_levelPrefabFilter, &QLineEdit::textChanged, this, [this]() { filterBrowser(m_levelPrefabList, m_levelPrefabFilter); });
	refreshLevelPrefabBrowser();
}

void ApplicationShell::refreshLevelAssetBrowsers()
{
	refreshLevelModelBrowser();
	refreshLevelSoundBrowser();
	refreshLevelPrefabBrowser();
}

void ApplicationShell::refreshLevelModelBrowser()
{
	if (!m_levelModelList) {
		return;
	}
	if (m_levelModelPlaceHint) {
		m_levelModelPlaceHint->setText(levelModelPlaceHint());
	}
	const PackageArchive& archive = packageViewArchive();
	const QString key = packageViewKey() + (archive.isOpen() ? QStringLiteral("|open") : QStringLiteral("|closed"));
	if (key != m_levelModelListKey) {
		m_levelModelListKey = key;
		const QString current = currentPayload(m_levelModelList);
		const QSignalBlocker blocker(m_levelModelList);
		m_levelModelList->clear();
		int count = 0;
		if (!archive.isOpen()) {
			addNote(m_levelModelList, tr("Open a package to browse its models."));
		} else {
			count = fillGroupedTree(m_levelModelList, archiveFilesOfKind(archive, AssetPreviewKind::Model), QStringLiteral("model"), QStringLiteral("cube"));
			if (count == 0) {
				addNote(m_levelModelList, tr("This package has no MDL, MD2, MD3 or OBJ models."));
			}
		}
		if (SidebarPage* page = levelSidebarPage(QStringLiteral("models"))) {
			page->setBadge(count > 0 ? QString::number(count) : QString());
		}
		selectPayload(m_levelModelList, current);
		filterBrowser(m_levelModelList, m_levelModelFilter);
		m_levelModelShownKey.clear();
	}
	showLevelModelPreview();
}

void ApplicationShell::refreshLevelAssetButtons()
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool entity = m_levelMapDocument.selectionKind == LevelMapSelectionKind::Entity;
	const bool model = currentPayload(m_levelModelList).startsWith(QStringLiteral("model:"));
	const bool sound = currentPayload(m_levelSoundList).startsWith(QStringLiteral("sound:"));
	if (m_levelModelPlace) {
		m_levelModelPlace->setEnabled(model && quake);
	}
	if (m_levelModelAssign) {
		m_levelModelAssign->setEnabled(model && quake && entity);
	}
	if (m_levelSoundPlay) {
		m_levelSoundPlay->setEnabled(sound);
	}
	if (m_levelSoundPlace) {
		m_levelSoundPlace->setEnabled(sound && quake);
	}
	if (m_levelSoundAssign) {
		m_levelSoundAssign->setEnabled(sound && quake && entity);
	}
}

void ApplicationShell::showLevelModelPreview()
{
	if (!m_levelModelList || !m_levelModelPreview) {
		return;
	}
	const QString payload = currentPayload(m_levelModelList);
	const QString path = payload.startsWith(QStringLiteral("model:")) ? payload.mid(6) : QString();
	refreshLevelAssetButtons();
	// Nothing decodes for a tab no one is looking at.
	SidebarPage* page = levelSidebarPage(QStringLiteral("models"));
	if (!page || !page->isVisible()) {
		return;
	}
	const QString key = packageViewKey() + QLatin1Char('|') + path + QLatin1Char('|') + activePaletteId();
	if (key == m_levelModelShownKey) {
		return;
	}
	m_levelModelShownKey = key;
	if (path.isEmpty() || !packageViewArchive().isOpen()) {
		if (m_levelModelWorker) {
			m_levelModelWorker->reset();
		}
		// The preview says to choose a model; the line below it stays out of the way.
		m_levelModelPreview->clearMesh();
		showHint(m_levelModelInfo, QString());
		return;
	}
	if (!m_levelModelWorker) {
		m_levelModelWorker = new ModelPreviewWorker(this);
		m_levelModelWorker->completed = [this](const ModelPreviewResult& result) {
			if (result.key != m_levelModelShownKey || !m_levelModelPreview) {
				return;
			}
			if (result.cancelled || !result.error.isEmpty()) {
				m_levelModelPreview->clearMesh();
				showHint(m_levelModelInfo, result.cancelled ? tr("Preview cancelled.") : result.error);
				return;
			}
			m_levelModelPreview->setMesh(result.mesh);
			QHash<int, QImage> images;
			for (int surface = 0; surface < result.mesh.surfaces.size(); ++surface) {
				for (const auto& material : result.assets.materials) {
					if (material.key == modelPreviewSurfaceMaterialKey(surface) && material.ready()) {
						images.insert(surface, material.image);
						break;
					}
				}
			}
			m_levelModelPreview->setSurfaceSkins(images);
			const auto preferences = m_settings.accessibilityPreferences();
			m_levelModelPreview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
			m_levelModelPreview->setReducedMotion(preferences.reducedMotion);
			m_levelModelPreview->frameModel();
			showHint(m_levelModelInfo, result.mesh.geometryAvailable
					? tr("%1 · %2 triangles · %3 × %4 × %5 units")
						  .arg(QFileInfo(result.path).fileName())
						  .arg(result.mesh.triangleCount)
						  .arg(qRound(result.mesh.maxs.x - result.mesh.mins.x))
						  .arg(qRound(result.mesh.maxs.y - result.mesh.mins.y))
						  .arg(qRound(result.mesh.maxs.z - result.mesh.mins.z))
					: result.mesh.warnings.join(QLatin1Char(' ')));
		};
	}
	m_levelModelPreview->clearMesh();
	showHint(m_levelModelInfo, tr("Loading %1…").arg(QFileInfo(path).fileName()));
	ModelMaterialSource source;
	source.archive = std::make_shared<PackageArchive>(packageViewArchive());
	source.revision = packageViewKey();
	source.paletteId = activePaletteId();
	m_levelModelWorker->request(std::move(source), path, key);
}

void ApplicationShell::refreshLevelSoundBrowser()
{
	if (!m_levelSoundList) {
		return;
	}
	const PackageArchive& archive = packageViewArchive();
	const QString key = packageViewKey() + (archive.isOpen() ? QStringLiteral("|open") : QStringLiteral("|closed"));
	if (m_levelSoundPlaceHint) {
		m_levelSoundPlaceHint->setText(levelSoundPlaceHint());
	}
	if (key != m_levelSoundListKey) {
		m_levelSoundListKey = key;
		stopLevelSoundPlayback();
		++m_levelSoundRevision;
		const QString current = currentPayload(m_levelSoundList);
		const QSignalBlocker blocker(m_levelSoundList);
		m_levelSoundList->clear();
		int count = 0;
		if (!archive.isOpen()) {
			addNote(m_levelSoundList, tr("Open a package to browse its sounds."));
		} else {
			count = fillGroupedTree(m_levelSoundList, archiveFilesOfKind(archive, AssetPreviewKind::Audio), QStringLiteral("sound"), QStringLiteral("speaker"));
			if (count == 0) {
				addNote(m_levelSoundList, tr("This package has no WAV, Ogg or Doom sounds."));
			}
		}
		if (SidebarPage* page = levelSidebarPage(QStringLiteral("sounds"))) {
			page->setBadge(count > 0 ? QString::number(count) : QString());
		}
		selectPayload(m_levelSoundList, current);
		filterBrowser(m_levelSoundList, m_levelSoundFilter);
		m_levelSoundShownPath.clear();
	}
	showLevelSoundPreview();
}

void ApplicationShell::showLevelSoundPreview()
{
	if (!m_levelSoundList || !m_levelSoundWave) {
		return;
	}
	const QTreeWidgetItem* item = m_levelSoundList->currentItem();
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	const QString path = payload.startsWith(QStringLiteral("sound:")) ? payload.mid(6) : QString();
	refreshLevelAssetButtons();
	SidebarPage* page = levelSidebarPage(QStringLiteral("sounds"));
	if (!page || !page->isVisible() || path == m_levelSoundShownPath) {
		return;
	}
	m_levelSoundShownPath = path;
	m_levelSoundWave->clearPeaks();
	// The waveform says what is going on until there is a waveform to show.
	showHint(m_levelSoundInfo, QString());
	if (path.isEmpty() || !packageViewArchive().isOpen()) {
		m_levelSoundWave->setEmptyText(path.isEmpty() ? tr("Choose a sound to see and hear it.") : QString());
		return;
	}
	m_levelSoundWave->setEmptyText(tr("Reading %1…").arg(QFileInfo(path).fileName()));
	AudioBrowserRequest request;
	request.kind = AudioBrowserRequest::Kind::Preview;
	request.archive = std::make_shared<PackageArchive>(packageViewArchive());
	request.revision = m_levelSoundRevision;
	request.entryIndex = item->data(0, kEntryIndexRole).toLongLong();
	request.virtualPath = path;
	m_levelSoundWorker->request(std::move(request));
}

void ApplicationShell::toggleLevelSoundPlayback()
{
	if (m_levelSoundPlayback && (m_levelSoundPlayback->state() == AudioPlayback::State::Playing || m_levelSoundPlayback->state() == AudioPlayback::State::Loading)) {
		stopLevelSoundPlayback();
		return;
	}
	const QTreeWidgetItem* item = m_levelSoundList ? m_levelSoundList->currentItem() : nullptr;
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	if (!payload.startsWith(QStringLiteral("sound:")) || !packageViewArchive().isOpen()) {
		statusBar()->showMessage(tr("Choose a sound to play."));
		return;
	}
	m_levelSoundShownPath = payload.mid(6);
	AudioBrowserRequest request;
	request.kind = AudioBrowserRequest::Kind::Audition;
	request.archive = std::make_shared<PackageArchive>(packageViewArchive());
	request.revision = m_levelSoundRevision;
	request.entryIndex = item->data(0, kEntryIndexRole).toLongLong();
	request.virtualPath = m_levelSoundShownPath;
	if (m_levelSoundPlay) {
		m_levelSoundPlay->setText(tr("Stop"));
		static_cast<QPushButton*>(m_levelSoundPlay)->setIcon(studioIcon(QStringLiteral("stop")));
	}
	statusBar()->showMessage(tr("Preparing %1 for playback…").arg(m_levelSoundShownPath));
	m_levelSoundAuditionWorker->request(std::move(request));
}

void ApplicationShell::stopLevelSoundPlayback()
{
	if (m_levelSoundAuditionWorker) {
		m_levelSoundAuditionWorker->cancel();
	}
	if (m_levelSoundPlayback) {
		m_levelSoundPlayback->stop();
	}
	if (m_levelSoundPlay) {
		m_levelSoundPlay->setText(tr("Play"));
		static_cast<QPushButton*>(m_levelSoundPlay)->setIcon(studioIcon(QStringLiteral("play")));
	}
	if (m_levelSoundWave) {
		m_levelSoundWave->setPlayhead(0);
	}
}

void ApplicationShell::refreshLevelPrefabBrowser()
{
	if (!m_levelPrefabList) {
		return;
	}
	const QString project = m_settings.currentProjectPath();
	const PackageArchive& archive = packageViewArchive();
	const QString key = project + QLatin1Char('|') + packageViewKey() + (archive.isOpen() ? QStringLiteral("|open") : QStringLiteral("|closed"));
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (m_levelPrefabInfo) {
		m_levelPrefabInfo->setText(quake ? tr("Prefabs keep their brushes, entities and asset references; inserting gives their target names fresh, unique values.")
										 : tr("Prefabs are for Quake-family maps."));
	}
	if (key == m_levelPrefabListKey) {
		return;
	}
	SidebarPage* page = levelSidebarPage(QStringLiteral("prefabs"));
	// The project folder is searched only once someone looks at the tab.
	if (!page || !page->isVisible()) {
		return;
	}
	m_levelPrefabListKey = key;
	const QString current = currentPayload(m_levelPrefabList);
	const QSignalBlocker blocker(m_levelPrefabList);
	m_levelPrefabList->clear();
	int count = 0;
	const QIcon prefabIcon = studioIcon(QStringLiteral("prefab"), StudioIconTone::Muted);
	if (!project.isEmpty() && QFileInfo(project).isDir()) {
		auto* group = new QTreeWidgetItem(m_levelPrefabList, {tr("Project")});
		group->setIcon(0, studioIcon(QStringLiteral("folder"), StudioIconTone::Muted));
		group->setFlags(Qt::ItemIsEnabled);
		group->setExpanded(true);
		QDirIterator files(project, {QStringLiteral("*.vprefab")}, QDir::Files, QDirIterator::Subdirectories);
		int visited = 0;
		while (files.hasNext() && visited < 2000) {
			const QString file = files.next();
			++visited;
			auto* item = new QTreeWidgetItem(group, {QFileInfo(file).completeBaseName()});
			item->setIcon(0, prefabIcon);
			item->setData(0, Qt::UserRole, QStringLiteral("prefab-file:") + file);
			item->setData(0, kPathRole, QDir(project).relativeFilePath(file));
			item->setToolTip(0, QDir::toNativeSeparators(file));
			++count;
		}
		if (group->childCount() == 0) {
			delete group;
		}
	}
	if (archive.isOpen()) {
		QTreeWidgetItem* group = nullptr;
		for (const PackageEntry& entry : archive.entries()) {
			if (entry.kind != PackageEntryKind::File || !entry.virtualPath.endsWith(QStringLiteral(".vprefab"), Qt::CaseInsensitive)) {
				continue;
			}
			if (!group) {
				group = new QTreeWidgetItem(m_levelPrefabList, {tr("Package")});
				group->setIcon(0, studioIcon(QStringLiteral("package"), StudioIconTone::Muted));
				group->setFlags(Qt::ItemIsEnabled);
				group->setExpanded(true);
			}
			auto* item = new QTreeWidgetItem(group, {QFileInfo(entry.virtualPath).completeBaseName()});
			item->setIcon(0, prefabIcon);
			item->setData(0, Qt::UserRole, QStringLiteral("prefab-entry:") + entry.virtualPath);
			item->setData(0, kPathRole, entry.virtualPath);
			item->setToolTip(0, entry.virtualPath);
			++count;
		}
	}
	if (count == 0) {
		addNote(m_levelPrefabList, project.isEmpty() ? tr("Open a project, or a package with prefabs, to see them here. Insert… opens any prefab file.")
													 : tr("No prefabs yet. Select some objects and choose Save Selection… to make one."));
	}
	if (page) {
		page->setBadge(count > 0 ? QString::number(count) : QString());
	}
	selectPayload(m_levelPrefabList, current);
	filterBrowser(m_levelPrefabList, m_levelPrefabFilter);
}

QString ApplicationShell::levelModelEntityClass() const
{
	// misc_model is the class q3map2 bakes models from, and the one the
	// Quake-family definitions most often declare.
	EntityClassDefinition definition;
	for (const QString& candidate : {QStringLiteral("misc_model"), QStringLiteral("misc_gamemodel")}) {
		if (m_entityDefinitions.classForName(candidate, &definition)) {
			return candidate;
		}
	}
	return QStringLiteral("misc_model");
}

QString ApplicationShell::levelSoundEntityClass() const
{
	// A loaded definition with a noise key names the speaker; target_speaker
	// is Quake II's and Quake III's own.
	for (const QString& candidate : {QStringLiteral("target_speaker"), QStringLiteral("ambient_generic"), QStringLiteral("play_sound")}) {
		EntityClassDefinition definition;
		if (m_entityDefinitions.classForName(candidate, &definition)) {
			return candidate;
		}
	}
	for (const EntityClassDefinition& definition : m_entityDefinitions.classes) {
		EntityKeyDefinition key;
		if (definition.kind == EntityClassKind::Point && definition.keyForName(QStringLiteral("noise"), &key)) {
			return definition.className;
		}
	}
	return QStringLiteral("target_speaker");
}

QString ApplicationShell::levelModelPlaceHint() const
{
	if (m_levelMapDocument.format == LevelMapFormat::DoomWad) {
		return tr("Doom maps have no model entities; things take their sprites from the game.");
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return tr("Open a Quake-family map to place models and sounds.");
	}
	const QString className = levelModelEntityClass();
	// q3map2 bakes misc_model whatever the game's definitions say.
	if (m_levelMapDocument.format == LevelMapFormat::Quake3Map) {
		return tr("Models are placed as %1 entities with their model key set; q3map2 bakes them into the map.").arg(className);
	}
	EntityClassDefinition definition;
	const QString placed = tr("Models are placed as %1 entities with their model key set.").arg(className);
	return m_entityDefinitions.classForName(className, &definition)
		? placed
		: placed + QLatin1Char(' ') + tr("No loaded definition declares %1, so make sure your game or mod has it.").arg(className);
}

QString ApplicationShell::levelSoundPlaceHint() const
{
	if (m_levelMapDocument.format == LevelMapFormat::DoomWad) {
		return tr("Doom maps take their sounds from sectors and things, not speakers.");
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return tr("Open a Quake-family map to place models and sounds.");
	}
	const QString className = levelSoundEntityClass();
	EntityClassDefinition definition;
	const QString placed = tr("Sounds are placed as %1 entities with their noise key set.").arg(className);
	return m_entityDefinitions.classForName(className, &definition)
		? placed
		: placed + QLatin1Char(' ') + tr("No loaded definition declares %1, so make sure your game or mod has it.").arg(className);
}

namespace {

// How a map names a package sound: Quake and Quake II play paths below
// sound/, Quake III gives the whole path.
QString soundKeyValue(const LevelMapDocument& document, const QString& path)
{
	if (document.format != LevelMapFormat::Quake3Map && path.startsWith(QStringLiteral("sound/"), Qt::CaseInsensitive)) {
		return path.mid(6);
	}
	return path;
}

} // namespace

bool ApplicationShell::placeLevelAsset(const QString& payload, const QPointF& viewPoint)
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!quake || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family map to place models and sounds."));
		return false;
	}
	QString className;
	QVector<LevelMapProperty> properties;
	QString what;
	if (payload.startsWith(QStringLiteral("model:"))) {
		className = levelModelEntityClass();
		properties.push_back({QStringLiteral("model"), payload.mid(6), 0});
		what = payload.mid(6);
	} else if (payload.startsWith(QStringLiteral("sound:"))) {
		className = levelSoundEntityClass();
		properties.push_back({QStringLiteral("noise"), soundKeyValue(m_levelMapDocument, payload.mid(6)), 0});
		what = payload.mid(6);
	} else {
		return false;
	}
	const QPointF point = viewPoint.x() < 0.0 ? QPointF(m_levelMapViewport->rect().center()) : viewPoint;
	LevelMapVec3 origin = m_levelMapViewport->worldPositionAt(point, levelMapHiddenAxisValue());
	int entityId = -1;
	QString error;
	if (!addLevelMapEntity(&m_levelMapDocument, className, origin, properties, &entityId, &error)) {
		statusBar()->showMessage(tr("Could not place it: %1").arg(error));
		return false;
	}
	// The new entity is selected, so its keys are in the inspector at once.
	setLevelMapSelection(&m_levelMapDocument, {{LevelMapSelectionKind::Entity, entityId}});
	recordActivity(tr("Level map entity added"), QStringLiteral("entity:%1 %2").arg(entityId).arg(className), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Placed %1 as %2 (entity:%3). Undo takes it out again.").arg(QFileInfo(what).fileName(), className).arg(entityId), 5000);
	return true;
}

bool ApplicationShell::assignLevelAsset(const QString& payload)
{
	QVector<int> entities;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::Entity) {
			entities << ref.objectId;
		}
	}
	if (entities.isEmpty()) {
		statusBar()->showMessage(tr("Select one or more entities to give them this."));
		return false;
	}
	QString key;
	QString value;
	if (payload.startsWith(QStringLiteral("model:"))) {
		key = QStringLiteral("model");
		value = payload.mid(6);
	} else if (payload.startsWith(QStringLiteral("sound:"))) {
		key = QStringLiteral("noise");
		value = soundKeyValue(m_levelMapDocument, payload.mid(6));
	} else {
		return false;
	}
	QString error;
	if (!setLevelMapEntitiesProperty(&m_levelMapDocument, entities, key, {value}, &error)) {
		statusBar()->showMessage(tr("Could not set %1: %2").arg(key, error));
		return false;
	}
	recordActivity(tr("Level map keys changed"), key, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Set %1 to %2 on %n entit(y)(ies) as one undo step.", nullptr, static_cast<int>(entities.size())).arg(key, value), 5000);
	return true;
}

} // namespace vibestudio
