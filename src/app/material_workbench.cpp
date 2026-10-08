#include "app/material_workbench.h"

#include "app/code_editor.h"
#include "app/material_graph_view.h"
#include "app/material_library_model.h"
#include "app/material_preview_view.h"
#include "app/material_property_panel.h"
#include "app/material_script_highlighter.h"
#include "app/material_tasks.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"
#include "app/syntax_highlight.h"
#include "app/ui_primitives.h"
#include "core/material_eval.h"
#include "core/material_script.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr int kReparseDelayMs = 250;
constexpr int kSwatchSide = 72;

MaterialDiagnostic diagnosticOf(MaterialDiagnosticSeverity severity, const QString& code, const QString& message)
{
	MaterialDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.message = message;
	return diagnostic;
}

QString severityName(MaterialDiagnosticSeverity severity)
{
	switch (severity) {
	case MaterialDiagnosticSeverity::Error:
		return MaterialWorkbench::tr("Error");
	case MaterialDiagnosticSeverity::Warning:
		return MaterialWorkbench::tr("Warning");
	case MaterialDiagnosticSeverity::Info:
		return MaterialWorkbench::tr("Note");
	}
	return {};
}

QString imageStatusName(const QString& status)
{
	if (status == QStringLiteral("image")) {
		return MaterialWorkbench::tr("Found");
	}
	if (status == QStringLiteral("composite")) {
		return MaterialWorkbench::tr("Composed from patches");
	}
	if (status == QStringLiteral("builtin")) {
		return MaterialWorkbench::tr("Built into the engine");
	}
	if (status == QStringLiteral("generated")) {
		return MaterialWorkbench::tr("Generated stand-in");
	}
	if (status == QStringLiteral("program")) {
		return MaterialWorkbench::tr("Image program");
	}
	if (status == QStringLiteral("video")) {
		return MaterialWorkbench::tr("Video (placeholder)");
	}
	if (status == QStringLiteral("render-target")) {
		return MaterialWorkbench::tr("Render target");
	}
	if (status == QStringLiteral("unreadable")) {
		return MaterialWorkbench::tr("Unreadable");
	}
	if (status == QStringLiteral("missing")) {
		return MaterialWorkbench::tr("Missing");
	}
	return status;
}

bool plainMaterialName(const QString& name)
{
	return !name.isEmpty() && std::none_of(name.cbegin(), name.cend(), [](QChar c) {
		return c.isSpace() || c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char('"');
	});
}

} // namespace

MaterialWorkbench::MaterialWorkbench(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("materialWorkbench"));
	m_cache = std::make_shared<MaterialImageCache>(256LL * 1024 * 1024);
	m_library = std::make_shared<MaterialLibrary>();
	m_scanLane = new MaterialTaskLane(this);
	m_imageLane = new MaterialTaskLane(this);
	m_reparseTimer = new QTimer(this);
	m_reparseTimer->setSingleShot(true);
	m_reparseTimer->setInterval(kReparseDelayMs);
	connect(m_reparseTimer, &QTimer::timeout, this, [this]() { reparse(false); });
	m_visibleTimer = new QTimer(this);
	m_visibleTimer->setSingleShot(true);
	m_visibleTimer->setInterval(120);
	connect(m_visibleTimer, &QTimer::timeout, this, &MaterialWorkbench::updateVisibleRows);
	buildUi();
	retranslate();
	applyTheme();
	refreshDocumentState();
}

MaterialWorkbench::~MaterialWorkbench()
{
	// Background work publishes into members; stop it before they go.
	delete m_scanLane;
	m_scanLane = nullptr;
	delete m_imageLane;
	m_imageLane = nullptr;
}

void MaterialWorkbench::setHost(MaterialWorkbenchHost host)
{
	m_host = std::move(host);
	refreshDocumentState();
}

// --- Layout ------------------------------------------------------------------

void MaterialWorkbench::buildUi()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);
	m_state = new LoadingPane(this);
	m_state->setObjectName(QStringLiteral("materialLibraryState"));
	m_state->setVisible(false);
	root->addWidget(m_state);

	m_mainSplitter = createSplitter(Qt::Horizontal, QStringLiteral("materialsSplitter"), tr("Materials layout"));
	m_mainSplitter->addWidget(buildLibraryPanel());
	m_editorSplitter = createSplitter(Qt::Vertical, QStringLiteral("materialsEditorSplitter"), tr("Material preview and editors"));
	m_editorSplitter->addWidget(buildPreviewPanel());
	m_editorSplitter->addWidget(buildEditorTabs());
	m_editorSplitter->setStretchFactor(0, 3);
	m_editorSplitter->setStretchFactor(1, 4);
	m_mainSplitter->addWidget(m_editorSplitter);
	m_mainSplitter->setStretchFactor(0, 2);
	m_mainSplitter->setStretchFactor(1, 5);
	root->addWidget(m_mainSplitter, 1);

	auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
	saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(saveShortcut, &QShortcut::activated, this, [this]() {
		if (hasUnsavedChanges()) {
			save();
		}
	});
	// Undo and redo reach the text from the graph and the properties too:
	// both edit the same document.
	for (QWidget* scope : {static_cast<QWidget*>(m_graph), static_cast<QWidget*>(m_properties)}) {
		auto* undo = new QShortcut(QKeySequence::Undo, scope);
		undo->setContext(Qt::WidgetWithChildrenShortcut);
		connect(undo, &QShortcut::activated, m_editor, &QPlainTextEdit::undo);
		auto* redo = new QShortcut(QKeySequence::Redo, scope);
		redo->setContext(Qt::WidgetWithChildrenShortcut);
		connect(redo, &QShortcut::activated, m_editor, &QPlainTextEdit::redo);
	}
}

QWidget* MaterialWorkbench::buildLibraryPanel()
{
	auto* panel = new QWidget(this);
	panel->setObjectName(QStringLiteral("materialLibraryPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	m_filter = new QLineEdit(panel);
	m_filter->setObjectName(QStringLiteral("materialFilter"));
	m_filter->setClearButtonEnabled(true);
	m_filter->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	clearOnEscape(m_filter);
	layout->addWidget(m_filter);

	auto* row = new QHBoxLayout;
	row->setSpacing(4);
	m_engineFilter = new QComboBox(panel);
	m_engineFilter->setObjectName(QStringLiteral("materialEngineFilter"));
	m_viewMode = createToolButton(QStringLiteral("list"), QString(), QString(), false);
	m_viewMode->setObjectName(QStringLiteral("materialViewMode"));
	m_viewMode->setCheckable(true);
	m_animateSwatches = createToolButton(QStringLiteral("film"), QString(), QString(), false);
	m_animateSwatches->setObjectName(QStringLiteral("materialAnimateSwatches"));
	m_animateSwatches->setCheckable(true);
	row->addWidget(m_engineFilter, 1);
	row->addWidget(m_viewMode);
	row->addWidget(m_animateSwatches);
	layout->addLayout(row);

	m_model = new MaterialLibraryModel(this);
	m_model->setThumbnailSide(kSwatchSide);
	m_proxy = new MaterialLibraryFilter(this);
	m_proxy->setSourceModel(m_model);
	m_libraryView = new QListView(panel);
	m_libraryView->setObjectName(QStringLiteral("materialLibrary"));
	m_libraryView->setModel(m_proxy);
	m_libraryView->setViewMode(QListView::IconMode);
	m_libraryView->setResizeMode(QListView::Adjust);
	m_libraryView->setMovement(QListView::Static);
	m_libraryView->setUniformItemSizes(true);
	m_libraryView->setWrapping(true);
	// One line, elided in the middle, keeps both ends of a name in view.
	m_libraryView->setWordWrap(false);
	m_libraryView->setTextElideMode(Qt::ElideMiddle);
	m_libraryView->setIconSize(QSize(kSwatchSide, kSwatchSide));
	m_libraryView->setGridSize(libraryGridSize());
	m_model->setItemSize(libraryGridSize() - QSize(6, 6));
	m_libraryView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_libraryView->setSpacing(4);
	m_libraryView->setMinimumWidth(180);
	layout->addWidget(m_libraryView, 1);

	m_libraryCount = new QLabel(panel);
	m_libraryCount->setObjectName(QStringLiteral("materialLibraryCount"));
	m_libraryCount->setForegroundRole(QPalette::PlaceholderText);
	m_libraryCount->setWordWrap(true);
	layout->addWidget(m_libraryCount);

	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_proxy->setQuery(text);
		updateLibraryCount();
		m_visibleTimer->start();
	});
	connect(m_engineFilter, &QComboBox::activated, this, [this](int) {
		m_proxy->setEngine(static_cast<MaterialEngine>(m_engineFilter->currentData().toInt()));
		updateLibraryCount();
		m_visibleTimer->start();
	});
	connect(m_viewMode, &QToolButton::toggled, this, [this](bool list) {
		m_libraryView->setViewMode(list ? QListView::ListMode : QListView::IconMode);
		m_libraryView->setIconSize(list ? QSize(28, 28) : QSize(kSwatchSide, kSwatchSide));
		m_libraryView->setGridSize(list ? QSize() : libraryGridSize());
		m_model->setItemSize(list ? QSize() : libraryGridSize() - QSize(6, 6));
		m_libraryView->setWrapping(!list);
		refreshViewModeButton();
		m_visibleTimer->start();
	});
	connect(m_animateSwatches, &QToolButton::toggled, this, [this](bool animate) {
		m_model->setAnimateThumbnails(animate && !m_reducedMotion);
		updateVisibleRows();
	});
	connect(m_libraryView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
		if (current.isValid()) {
			selectEntry(m_proxy->mapToSource(current).row());
		}
	});
	connect(m_libraryView, &QListView::activated, this, [this](const QModelIndex&) { m_preview->setFocus(Qt::OtherFocusReason); });
	connect(m_libraryView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { m_visibleTimer->start(); });
	return panel;
}

QWidget* MaterialWorkbench::buildPreviewPanel()
{
	auto* panel = new QWidget(this);
	panel->setObjectName(QStringLiteral("materialPreviewPanel"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	m_materialTitle = new QLabel(panel);
	m_materialTitle->setObjectName(QStringLiteral("materialTitle"));
	QFont titleFont = m_materialTitle->font();
	titleFont.setBold(true);
	m_materialTitle->setFont(titleFont);
	m_materialTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_materialTitle->setWordWrap(true);
	layout->addWidget(m_materialTitle);

	auto* controls = new QHBoxLayout;
	controls->setSpacing(4);
	m_play = createToolButton(QStringLiteral("play"), QString(), QString(), false);
	m_play->setObjectName(QStringLiteral("materialPlay"));
	m_restart = createToolButton(QStringLiteral("repeat"), QString(), QString(), false);
	m_restart->setObjectName(QStringLiteral("materialRestart"));
	m_speed = new QComboBox(panel);
	m_speed->setObjectName(QStringLiteral("materialSpeed"));
	for (const double speed : {0.25, 0.5, 1.0, 2.0, 4.0}) {
		m_speed->addItem(QString(), speed);
	}
	m_speed->setCurrentIndex(2);
	m_timeline = new QSlider(Qt::Horizontal, panel);
	m_timeline->setObjectName(QStringLiteral("materialTimeline"));
	m_timeline->setRange(0, 1000);
	m_timeline->setPageStep(100);
	m_timeLabel = new QLabel(panel);
	m_timeLabel->setObjectName(QStringLiteral("materialTime"));
	m_timeLabel->setMinimumWidth(m_timeLabel->fontMetrics().horizontalAdvance(QStringLiteral("000.0 s")));
	m_shape = new QComboBox(panel);
	m_shape->setObjectName(QStringLiteral("materialShape"));
	for (MaterialPreviewShape shape : materialPreviewShapes()) {
		m_shape->addItem(QString(), static_cast<int>(shape));
	}
	m_lighting = createToolButton(QStringLiteral("settings"), QString(), QString(), true);
	m_lighting->setObjectName(QStringLiteral("materialEngineView"));
	m_lightingMenu = new QMenu(m_lighting);
	m_lighting->setMenu(m_lightingMenu);
	m_lighting->setPopupMode(QToolButton::InstantPopup);
	controls->addWidget(m_play);
	controls->addWidget(m_restart);
	controls->addWidget(m_speed);
	controls->addWidget(m_timeline, 1);
	controls->addWidget(m_timeLabel);
	controls->addWidget(m_shape);
	controls->addWidget(m_lighting);
	layout->addLayout(controls);

	m_preview = new MaterialPreviewView(panel);
	layout->addWidget(m_preview, 1);

	m_notes = new QLabel(panel);
	m_notes->setObjectName(QStringLiteral("materialNotes"));
	m_notes->setWordWrap(true);
	m_notes->setForegroundRole(QPalette::PlaceholderText);
	m_notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_notes);

	connect(m_play, &QToolButton::clicked, this, [this]() { m_preview->setPlaying(!m_preview->isPlaying()); });
	connect(m_restart, &QToolButton::clicked, this, [this]() { m_preview->setTime(0.0); });
	connect(m_speed, &QComboBox::activated, this, [this](int) { m_preview->setSpeed(m_speed->currentData().toDouble()); });
	connect(m_timeline, &QSlider::valueChanged, this, [this](int value) {
		if (m_timeline->isSliderDown() || m_timeline->hasFocus()) {
			m_preview->setTime(value / 1000.0 * cycleSeconds());
		}
	});
	connect(m_shape, &QComboBox::activated, this, [this](int) {
		m_shapeChosen = true;
		m_preview->setShape(static_cast<MaterialPreviewShape>(m_shape->currentData().toInt()));
	});
	connect(m_preview, &MaterialPreviewView::playingChanged, this, [this](bool) { refreshPlayButton(); });
	connect(m_preview, &MaterialPreviewView::timeChanged, this, [this](double) { updateTimeline(); });
	connect(m_preview, &MaterialPreviewView::frameRendered, this, &MaterialWorkbench::updateNotes);
	return panel;
}

QWidget* MaterialWorkbench::buildEditorTabs()
{
	m_tabs = createPanelTabs(tr("Material editors"));
	m_tabs->setObjectName(QStringLiteral("materialEditorTabs"));

	// Nodes.
	auto* nodes = new QWidget(m_tabs);
	nodes->setObjectName(QStringLiteral("materialNodesTab"));
	auto* nodesLayout = new QVBoxLayout(nodes);
	nodesLayout->setContentsMargins(0, 4, 0, 0);
	nodesLayout->setSpacing(4);
	auto* nodeBar = new QHBoxLayout;
	m_addNode = createToolButton(QStringLiteral("add"), QString(), QString(), true);
	m_addNode->setObjectName(QStringLiteral("materialAddNode"));
	m_addNodeMenu = new QMenu(m_addNode);
	m_addNodeMenu->setToolTipsVisible(true);
	m_addNode->setMenu(m_addNodeMenu);
	m_addNode->setPopupMode(QToolButton::InstantPopup);
	auto* fit = createToolButton(QStringLiteral("fit"), QString(), QString(), false);
	fit->setObjectName(QStringLiteral("materialFitGraph"));
	m_newMaterial = createToolButton(QStringLiteral("plus"), QString(), QString(), true);
	m_newMaterial->setObjectName(QStringLiteral("materialNew"));
	m_save = createToolButton(QStringLiteral("save"), QString(), QString(), true);
	m_save->setObjectName(QStringLiteral("materialSave"));
	m_revert = createToolButton(QStringLiteral("undo"), QString(), QString(), false);
	m_revert->setObjectName(QStringLiteral("materialRevert"));
	nodeBar->addWidget(m_addNode);
	nodeBar->addWidget(fit);
	nodeBar->addStretch(1);
	nodeBar->addWidget(m_newMaterial);
	nodeBar->addWidget(m_revert);
	nodeBar->addWidget(m_save);
	nodesLayout->addLayout(nodeBar);
	auto* graphSplitter = createSplitter(Qt::Horizontal, QStringLiteral("materialGraphSplitter"), tr("Node graph and properties"));
	m_graph = new MaterialGraphView(graphSplitter);
	m_properties = new MaterialPropertyPanel(graphSplitter);
	graphSplitter->addWidget(m_graph);
	graphSplitter->addWidget(m_properties);
	graphSplitter->setStretchFactor(0, 3);
	graphSplitter->setStretchFactor(1, 1);
	nodesLayout->addWidget(graphSplitter, 1);

	// Text.
	auto* text = new QWidget(m_tabs);
	text->setObjectName(QStringLiteral("materialTextTab"));
	auto* textLayout = new QVBoxLayout(text);
	textLayout->setContentsMargins(0, 4, 0, 0);
	textLayout->setSpacing(4);
	m_documentLabel = new QLabel(text);
	m_documentLabel->setObjectName(QStringLiteral("materialDocument"));
	m_documentLabel->setForegroundRole(QPalette::PlaceholderText);
	m_documentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_documentLabel->setWordWrap(true);
	m_editor = new StudioCodeEditor(text);
	m_editor->setObjectName(QStringLiteral("materialEditor"));
	m_editor->setBaseFont(studioMonospaceFont());
	m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_highlighter = new MaterialScriptHighlighter(m_editor->document());
	textLayout->addWidget(m_documentLabel);
	textLayout->addWidget(m_editor, 1);

	// Images.
	m_imageList = new QTreeWidget(m_tabs);
	m_imageList->setObjectName(QStringLiteral("materialImages"));
	m_imageList->setColumnCount(4);
	m_imageList->setRootIsDecorated(false);
	m_imageList->setUniformRowHeights(true);
	m_imageList->setIconSize(QSize(32, 32));
	m_imageList->header()->setSectionResizeMode(0, QHeaderView::Stretch);

	// Problems.
	m_problems = new QTreeWidget(m_tabs);
	m_problems->setObjectName(QStringLiteral("materialProblems"));
	m_problems->setColumnCount(3);
	m_problems->setRootIsDecorated(false);
	m_problems->setUniformRowHeights(true);
	m_problems->header()->setStretchLastSection(true);

	// Details.
	m_details = new QTextEdit(m_tabs);
	m_details->setObjectName(QStringLiteral("materialDetails"));
	m_details->setReadOnly(true);
	applyMonospaceContentFont(m_details);

	m_tabs->addTab(nodes, studioIcon(QStringLiteral("shaders")), QString());
	m_tabs->addTab(text, studioIcon(QStringLiteral("code")), QString());
	m_tabs->addTab(m_imageList, studioIcon(QStringLiteral("image")), QString());
	m_tabs->addTab(m_problems, studioIcon(QStringLiteral("diagnostics")), QString());
	m_tabs->addTab(m_details, studioIcon(QStringLiteral("details")), QString());

	connect(fit, &QToolButton::clicked, m_graph, &MaterialGraphView::fitToView);
	connect(m_newMaterial, &QToolButton::clicked, this, &MaterialWorkbench::showNewMaterialDialog);
	connect(m_save, &QToolButton::clicked, this, [this]() {
		if (m_document.filePath.isEmpty() && m_document.virtualPath.isEmpty()) {
			chooseSaveAs();
		} else {
			save();
		}
	});
	connect(m_revert, &QToolButton::clicked, this, &MaterialWorkbench::revert);
	connect(m_graph, &MaterialGraphView::selectionChanged, this, [this](const QString& id) {
		m_properties->setNode(m_graph->graph().node(id), m_graph->isReadOnly());
	});
	connect(m_graph, &MaterialGraphView::editRequested, this, [this](const MaterialGraphEdit& edit) { applyGraphEdit(edit); });
	connect(m_graph, &MaterialGraphView::nodeActivated, this, [this](const QString&) { m_properties->focusFirstField(); });
	connect(m_graph, &MaterialGraphView::showTextRequested, this, [this, text](int line) {
		m_tabs->setCurrentWidget(text);
		showLine(line);
	});
	connect(m_graph, &MaterialGraphView::showImageRequested, this, [this](const QString& reference) {
		if (m_host.showTexture) {
			m_host.showTexture(reference);
		}
	});
	connect(m_properties, &MaterialPropertyPanel::propertyEdited, this, [this](const QString& node, const QString& property, const QString& value) {
		MaterialGraphEdit edit;
		edit.kind = MaterialGraphEditKind::SetProperty;
		edit.node = node;
		edit.property = property;
		edit.value = value;
		applyGraphEdit(edit);
	});
	connect(m_properties, &MaterialPropertyPanel::showImageRequested, this, [this](const QString& reference) {
		if (m_host.showTexture) {
			m_host.showTexture(reference);
		}
	});
	connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
		// Highlighting changes formats and fires textChanged too; only a
		// change to the text itself needs parsing.
		if (!m_applyingText && m_editor->toPlainText() != m_parsedText) {
			scheduleReparse();
			refreshDocumentState();
		}
	});
	connect(m_problems, &QTreeWidget::itemActivated, this, [this, text](QTreeWidgetItem* item) {
		const int line = item ? item->data(0, Qt::UserRole).toInt() : 0;
		if (line > 0) {
			m_tabs->setCurrentWidget(text);
			showLine(line);
		}
	});
	connect(m_imageList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		if (item && m_host.showTexture) {
			m_host.showTexture(item->data(0, Qt::UserRole).toString());
		}
	});
	return m_tabs;
}

void MaterialWorkbench::retranslate()
{
	setAccessibleName(tr("Materials workbench"));
	m_state->setAccessibleName(tr("Material library state"));
	m_filter->setPlaceholderText(tr("Filter materials"));
	m_filter->setAccessibleName(tr("Material filter"));
	m_filter->setToolTip(tr("Words search names. key=value, key:text, key<n and key>n test engine, kind, source, stages, images, frames, blend, "
							"sort, cull, errors and warnings; animated, sky, light, fog, translucent, rejected and shadowed take yes or no; "
							"surface parameters and flags too: animated=yes, engine=doom3, surfaceparm=nolightmap."));
	m_engineFilter->setAccessibleName(tr("Engine"));
	m_engineFilter->setToolTip(tr("Show the materials of one engine."));
	refreshViewModeButton();
	m_animateSwatches->setText(tr("Animate Swatches"));
	m_animateSwatches->setToolTip(tr("Play animated materials in the library while they are on screen."));
	m_animateSwatches->setAccessibleName(tr("Animate swatches"));
	m_libraryView->setAccessibleName(tr("Material library"));
	m_libraryView->setAccessibleDescription(tr("Every texture, shader and material in the open package. Arrow keys choose one; "
											   "Enter moves to its preview."));
	m_materialTitle->setAccessibleName(tr("Material"));
	refreshPlayButton();
	m_restart->setText(tr("Restart"));
	m_restart->setToolTip(tr("Go back to time zero."));
	m_restart->setAccessibleName(tr("Restart animation"));
	const QStringList speeds {tr("0.25x"), tr("0.5x"), tr("1x"), tr("2x"), tr("4x")};
	for (int index = 0; index < m_speed->count() && index < speeds.size(); ++index) {
		m_speed->setItemText(index, speeds.at(index));
	}
	m_speed->setAccessibleName(tr("Playback speed"));
	m_speed->setToolTip(tr("How fast time runs in the preview."));
	m_timeline->setAccessibleName(tr("Time"));
	m_timeline->setToolTip(tr("Scrub through the material's animation."));
	for (int index = 0; index < m_shape->count(); ++index) {
		m_shape->setItemText(index, materialPreviewShapeDisplayName(static_cast<MaterialPreviewShape>(m_shape->itemData(index).toInt())));
	}
	m_shape->setAccessibleName(tr("Preview shape"));
	m_shape->setToolTip(tr("The surface the material is drawn on."));
	m_lighting->setText(tr("Engine View"));
	m_lighting->setToolTip(tr("Lighting and drawing choices of the material's engine: overbright, sector light, light styles, renderer, "
							  "the Doom 3 light."));
	m_lighting->setAccessibleName(tr("Engine view options"));
	m_notes->setAccessibleName(tr("Preview notes"));
	m_tabs->setAccessibleName(tr("Material editors"));
	refreshTabTexts();
	if (auto* fit = findChild<QToolButton*>(QStringLiteral("materialFitGraph"))) {
		fit->setText(tr("Fit Graph"));
		fit->setToolTip(tr("Fit the whole graph in view (Ctrl+0 in the graph)."));
		fit->setAccessibleName(tr("Fit graph"));
	}
	m_addNode->setText(tr("Add Node"));
	m_addNode->setToolTip(tr("Add a node to the material or to the selected node's stage."));
	m_addNode->setAccessibleName(tr("Add node"));
	m_newMaterial->setText(tr("New Material"));
	m_newMaterial->setToolTip(tr("Start a material from a template."));
	m_newMaterial->setAccessibleName(tr("New material"));
	m_save->setText(tr("Save"));
	m_save->setAccessibleName(tr("Save material text"));
	m_revert->setText(tr("Revert"));
	m_revert->setToolTip(tr("Go back to the saved text; Undo brings the edits back."));
	m_revert->setAccessibleName(tr("Revert to saved"));
	m_editor->setAccessibleName(tr("Material text"));
	m_editor->setAccessibleDescription(tr("The script the graph shows. Every change updates the preview and the graph."));
	m_imageList->setHeaderLabels({tr("Image"), tr("Status"), tr("Size"), tr("Read from")});
	m_imageList->setAccessibleName(tr("Material images"));
	m_imageList->setAccessibleDescription(tr("Every image the material reads and how the engine finds it. Enter shows one on the Textures page."));
	m_problems->setHeaderLabels({tr("Severity"), tr("Line"), tr("Problem")});
	m_problems->setAccessibleName(tr("Material problems"));
	m_problems->setAccessibleDescription(tr("What the engine would warn about or reject. Enter goes to the line."));
	m_details->setAccessibleName(tr("Material details"));
	updateLibraryCount();
	refreshDocumentState();
}

void MaterialWorkbench::refreshPlayButton()
{
	const bool playing = m_preview->isPlaying();
	m_play->setIcon(studioIcon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
	m_play->setText(playing ? tr("Pause") : tr("Play"));
	m_play->setToolTip(playing ? tr("Pause the animation (Space in the preview).") : tr("Play the animation in real time (Space in the preview)."));
	m_play->setAccessibleName(playing ? tr("Pause animation") : tr("Play animation"));
}

void MaterialWorkbench::refreshViewModeButton()
{
	const bool list = m_viewMode->isChecked();
	m_viewMode->setIcon(studioIcon(list ? QStringLiteral("grid") : QStringLiteral("list")));
	m_viewMode->setText(list ? tr("Show as Grid") : tr("Show as List"));
	m_viewMode->setToolTip(list ? tr("Show swatches in a grid.") : tr("Show materials as a list."));
	m_viewMode->setAccessibleName(tr("List view"));
}

void MaterialWorkbench::refreshTabTexts()
{
	setPanelTabText(m_tabs, 0, tr("Nodes"));
	setPanelTabText(m_tabs, 1, tr("Text"));
	setPanelTabText(m_tabs, 2, m_imageCount > 0 ? tr("Images (%1)").arg(m_imageCount) : tr("Images"));
	setPanelTabText(m_tabs, 3, m_problemCount > 0 ? tr("Problems (%1)").arg(m_problemCount) : tr("Problems"));
	setPanelTabText(m_tabs, 4, tr("Details"));
}

void MaterialWorkbench::applyTheme()
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	m_highlighter->setTheme(studioSyntaxTheme(tokens.light, tokens.highContrast));
}

void MaterialWorkbench::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	switch (event->type()) {
	case QEvent::LanguageChange:
		retranslate();
		rebuildLightingMenu();
		updateGraph();
		updateDetails();
		break;
	case QEvent::PaletteChange:
	case QEvent::StyleChange:
	case QEvent::FontChange:
		applyTheme();
		if (!m_viewMode->isChecked()) {
			m_libraryView->setGridSize(libraryGridSize());
			m_model->setItemSize(libraryGridSize() - QSize(6, 6));
		}
		break;
	default:
		break;
	}
}

QSize MaterialWorkbench::libraryGridSize() const
{
	// Room for the swatch and a line of name, wider when text is larger.
	const QFontMetrics metrics(m_libraryView->font());
	return {std::max(kSwatchSide + 36, metrics.averageCharWidth() * 14), kSwatchSide + metrics.height() + 14};
}

// --- Sources -----------------------------------------------------------------

void MaterialWorkbench::setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QString& path, const QString& revision)
{
	// A new snapshot of the same revision holds the same files.
	if (!revision.isEmpty() && revision == m_revision && path == m_packagePath) {
		return;
	}
	m_reader = std::move(reader);
	m_packagePath = path;
	m_revision = revision;
	m_pathIndex.clear();
	m_pathIndexReader = nullptr;
	requestScan();
}

void MaterialWorkbench::clearPackage()
{
	if (!m_reader && m_packagePath.isEmpty()) {
		return;
	}
	m_reader.reset();
	m_packagePath.clear();
	m_revision.clear();
	m_pathIndex.clear();
	m_pathIndexReader = nullptr;
	requestScan();
}

bool MaterialWorkbench::openScript(const QString& path, QString* error)
{
	const QFileInfo info(path);
	if (!info.isFile() || !isMaterialScriptPath(path)) {
		if (error) {
			*error = tr("%1 is not a .shader or .mtr script.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	m_looseFile = info.absoluteFilePath();
	m_materialName.clear();
	requestScan();
	return true;
}

void MaterialWorkbench::requestScan()
{
	// A big package takes a while to read; only read it once it is seen.
	if (isVisible()) {
		startScan();
	} else {
		m_scanPending = true;
		Q_EMIT contentChanged();
	}
}

void MaterialWorkbench::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (m_scanPending) {
		startScan();
	}
}

void MaterialWorkbench::setReducedMotion(bool reduced)
{
	m_reducedMotion = reduced;
	m_preview->setReducedMotion(reduced);
	m_model->setAnimateThumbnails(m_animateSwatches->isChecked() && !reduced);
	m_state->setReducedMotion(reduced);
}

void MaterialWorkbench::startScan()
{
	m_scanPending = false;
	m_scanning = true;
	m_state->setVisible(true);
	m_state->setTitle(tr("Reading materials"));
	m_state->setDetail(!m_packagePath.isEmpty() ? QDir::toNativeSeparators(m_packagePath) : QDir::toNativeSeparators(m_looseFile));
	m_state->setState(OperationState::Running, tr("Scanning"));
	m_state->setProgress({});
	const std::shared_ptr<const PackageArchiveReader> reader = m_reader;
	const QString looseFile = m_looseFile;
	const QString select = m_materialName;
	QPointer<MaterialWorkbench> self(this);
	m_scanLane->submit([this, reader, looseFile, select, self](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
		auto library = std::make_shared<MaterialLibrary>();
		if (reader) {
			QElapsedTimer throttle;
			throttle.start();
			*library = scanMaterialLibrary(*reader, MaterialLibraryOptions(),
				[cancelled, self, throttle](int done, int total, const QString& phase) mutable {
					if (throttle.elapsed() > 100 || done == total) {
						throttle.restart();
						QMetaObject::invokeMethod(
							QCoreApplication::instance(),
							[self, done, total, phase]() {
								if (self && self->m_scanning) {
									self->m_state->setProgress({done, total});
									self->m_state->setDetail(phase);
								}
							},
							Qt::QueuedConnection);
					}
					return !cancelled();
				});
		}
		int loose = -1;
		if (!looseFile.isEmpty()) {
			MaterialScript script;
			if (loadMaterialScript(looseFile, &script)) {
				const QString root = reader ? reader->sourcePath() : QString();
				loose = library->adoptScript(script, reader ? materialScriptVirtualPath(looseFile, root, script.engine) : QString());
			}
		}
		if (library->sourcePath.isEmpty()) {
			library->sourcePath = reader ? reader->sourcePath() : looseFile;
		}
		if (cancelled()) {
			return {};
		}
		const MaterialLibraryRows rows = prepareMaterialLibraryRows(library);
		return [this, library, rows, loose, select]() { applyLibrary(library, rows, loose, select); };
	});
	Q_EMIT contentChanged();
}

void MaterialWorkbench::applyLibrary(std::shared_ptr<MaterialLibrary> library, const MaterialLibraryRows& rows, int looseScript, const QString& select)
{
	m_scanning = false;
	m_library = std::move(library);
	m_looseScript = looseScript;
	m_model->setRows(rows, imageSource(), m_cache);
	rebuildEngineFilter();
	m_properties->setImageChoices(packageImages());
	const int total = static_cast<int>(m_library->entries.size());
	if (!m_library->warnings.isEmpty()) {
		m_state->setTitle(tr("Materials read with warnings"));
		m_state->setDetail(m_library->warnings.join(QLatin1Char('\n')));
		const int warnings = static_cast<int>(m_library->warnings.size());
		m_state->setState(OperationState::Warning, tr("%n warning(s)", nullptr, warnings));
		m_state->setVisible(true);
	} else {
		m_state->setState(OperationState::Completed, tr("Ready"));
		m_state->setVisible(false);
	}
	updateLibraryCount();
	// A material asked for while reading comes first; then the one in view;
	// then the loose script's first, then the library's first.
	int entry = -1;
	if (!m_requestedName.isEmpty()) {
		const QString requested = m_requestedName;
		m_requestedName.clear();
		entry = m_library->indexOf(requested);
		if (entry < 0 && !requested.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
			entry = m_library->indexOf(QStringLiteral("textures/") + requested);
		}
		if (entry < 0) {
			m_filter->setText(requested);
		}
	}
	if (entry < 0 && !select.isEmpty()) {
		entry = m_library->indexOf(select, m_engine);
		if (looseScript >= 0) {
			for (int index = 0; index < total; ++index) {
				if (m_library->entries.at(index).scriptIndex == looseScript
					&& materialLookupKey(m_library->entries.at(index).name) == materialLookupKey(select)) {
					entry = index;
					break;
				}
			}
		}
	}
	if (entry < 0 && looseScript >= 0) {
		for (int index = 0; index < total && entry < 0; ++index) {
			if (m_library->entries.at(index).scriptIndex == looseScript) {
				entry = index;
			}
		}
	}
	if (entry < 0 && total > 0) {
		entry = 0;
	}
	// With a filter typed, prefer a material the filter keeps.
	if (entry >= 0 && !m_proxy->mapFromSource(m_model->index(entry)).isValid() && m_proxy->rowCount() > 0) {
		entry = m_proxy->mapToSource(m_proxy->index(0, 0)).row();
	}
	if (entry >= 0) {
		const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
		if (proxyIndex.isValid()) {
			m_libraryView->setCurrentIndex(proxyIndex);
			m_libraryView->scrollTo(proxyIndex);
		}
		// The selection signal does not fire when the row stays current.
		selectEntry(entry);
	} else {
		m_materialName.clear();
		m_preview->clearMaterial(m_reader || !m_looseFile.isEmpty() ? tr("This package has no textures, shaders or materials.")
																	: tr("Open a package or a .shader or .mtr script to see its materials."));
		m_graph->clearGraph(tr("No material selected."));
		m_properties->setNode(nullptr, true);
	}
	m_visibleTimer->start();
	Q_EMIT contentChanged();
}

void MaterialWorkbench::rebuildEngineFilter()
{
	const MaterialEngine current = static_cast<MaterialEngine>(m_engineFilter->currentData().toInt());
	m_engineFilter->clear();
	m_engineFilter->addItem(tr("All engines"), static_cast<int>(MaterialEngine::Unknown));
	for (MaterialEngine engine : m_library->engines()) {
		const int count = m_library->count(engine);
		m_engineFilter->addItem(tr("%1 (%2)").arg(materialEngineDisplayName(engine)).arg(count), static_cast<int>(engine));
	}
	const int keep = m_engineFilter->findData(static_cast<int>(current));
	m_engineFilter->setCurrentIndex(std::max(0, keep));
	m_proxy->setEngine(static_cast<MaterialEngine>(m_engineFilter->currentData().toInt()));
	m_engineFilter->setVisible(m_engineFilter->count() > 2 || current != MaterialEngine::Unknown);
}

void MaterialWorkbench::updateLibraryCount()
{
	if (!m_libraryCount || !m_proxy) {
		return;
	}
	const int total = m_model->rowCount();
	const int shown = m_proxy->rowCount();
	QString text;
	if (m_scanning) {
		text = tr("Reading materials...");
	} else if (total == 0) {
		text = tr("No materials.");
	} else if (shown == total) {
		text = tr("%n material(s)", nullptr, total);
	} else {
		text = tr("Showing %1 of %n material(s)", nullptr, total).arg(shown);
		const QStringList unknown = m_proxy->unknownKeys();
		if (shown == 0 && !unknown.isEmpty()) {
			text += QLatin1Char(' ') + tr("No material has %1.").arg(unknown.join(QStringLiteral(", ")));
		}
	}
	m_libraryCount->setText(text);
	m_libraryCount->setAccessibleName(text);
}

void MaterialWorkbench::updateVisibleRows()
{
	QVector<int> rows;
	const QRect area = m_libraryView->viewport()->rect();
	QSize step = m_libraryView->gridSize();
	if (!step.isValid() || step.isEmpty()) {
		step = QSize(std::max(24, area.width()), m_libraryView->sizeHintForRow(0) > 0 ? m_libraryView->sizeHintForRow(0) : 24);
	}
	QSet<int> seen;
	for (int y = step.height() / 2; y < area.height() + step.height(); y += std::max(8, step.height())) {
		for (int x = step.width() / 2; x < area.width() + step.width(); x += std::max(8, step.width())) {
			const QModelIndex index = m_libraryView->indexAt(QPoint(std::min(x, area.width() - 1), std::min(y, area.height() - 1)));
			if (index.isValid()) {
				const int row = m_proxy->mapToSource(index).row();
				if (!seen.contains(row)) {
					seen.insert(row);
					rows.push_back(row);
				}
			}
		}
	}
	m_model->setVisibleRows(rows);
}

MaterialImageSource MaterialWorkbench::imageSource() const
{
	MaterialImageSource source;
	source.archive = m_reader;
	source.revision = m_revision.isEmpty() ? m_packagePath + QLatin1Char('|') + m_looseFile : m_revision;
	source.doomCatalog = m_library ? m_library->doomCatalog : nullptr;
	source.developerDefault = m_developerDefault;
	source.cache = m_cache.get();
	return source;
}

QStringList MaterialWorkbench::packageImages() const
{
	QStringList images;
	if (!m_reader) {
		return images;
	}
	static const QStringList suffixes {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("pcx"), QStringLiteral("bmp"), QStringLiteral("dds"), QStringLiteral("wal")};
	for (const PackageEntry& entry : m_reader->entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const QString suffix = QFileInfo(entry.virtualPath).suffix().toLower();
		if (suffixes.contains(suffix)) {
			images << entry.virtualPath;
		}
	}
	images.sort(Qt::CaseInsensitive);
	return images;
}

bool MaterialWorkbench::readPackageFile(const QString& path, QByteArray* bytes, QString* error) const
{
	if (!m_reader) {
		if (error) {
			*error = tr("No package is open.");
		}
		return false;
	}
	if (m_pathIndexReader != m_reader.get()) {
		m_pathIndex.clear();
		for (const PackageEntry& entry : m_reader->entries()) {
			if (entry.kind == PackageEntryKind::File) {
				m_pathIndex.insert(entry.virtualPath.toLower(), entry.virtualPath);
			}
		}
		m_pathIndexReader = m_reader.get();
	}
	const QString actual = m_pathIndex.value(path.toLower());
	if (actual.isEmpty()) {
		if (error) {
			*error = tr("%1 is not in the package.").arg(path);
		}
		return false;
	}
	return m_reader->readEntryBytes(actual, bytes, error, 16LL * 1024 * 1024);
}

bool MaterialWorkbench::packageIsWad() const
{
	return m_reader && m_reader->format() == PackageArchiveFormat::Wad;
}

QString MaterialWorkbench::freeScriptPath(MaterialEngine engine) const
{
	const QString folder = engine == MaterialEngine::Doom3 ? QStringLiteral("materials/") : QStringLiteral("scripts/");
	const QString suffix = engine == MaterialEngine::Doom3 ? QStringLiteral(".mtr") : QStringLiteral(".shader");
	for (int number = 0; number < 1000; ++number) {
		const QString candidate = folder + QStringLiteral("vibestudio") + (number == 0 ? QString() : QString::number(number)) + suffix;
		bool used = false;
		for (const MaterialScript& script : m_library->scripts) {
			used = used || script.path.compare(candidate, Qt::CaseInsensitive) == 0;
		}
		if (!used) {
			return candidate;
		}
	}
	return folder + QStringLiteral("vibestudio") + suffix;
}

// --- Selection and documents -------------------------------------------------

bool MaterialWorkbench::hasContent() const
{
	return m_scanning || m_scanPending || (m_library && !m_library->entries.isEmpty()) || m_document.kind != MaterialTextKind::None;
}

QString MaterialWorkbench::summary() const
{
	if (m_scanning || m_scanPending) {
		return tr("Reading materials...");
	}
	const int count = m_library ? static_cast<int>(m_library->entries.size()) : 0;
	if (count == 0) {
		return tr("No materials");
	}
	QStringList engines;
	for (MaterialEngine engine : m_library->engines()) {
		engines << materialEngineDisplayName(engine);
	}
	QString text = tr("%n material(s)", nullptr, count);
	if (!engines.isEmpty()) {
		text += QStringLiteral(" · ") + engines.join(QStringLiteral(", "));
	}
	if (hasUnsavedChanges()) {
		text += QStringLiteral(" · ") + tr("unsaved changes");
	}
	return text;
}

QString MaterialWorkbench::busyReason() const
{
	if (m_scanning || m_scanPending) {
		return QStringLiteral("scanning");
	}
	if (m_imageLane && m_imageLane->pending()) {
		return QStringLiteral("images");
	}
	if (!m_preview->isPlaying() && m_preview->renderPending()) {
		return QStringLiteral("drawing");
	}
	if (m_reparseTimer->isActive()) {
		return QStringLiteral("parsing");
	}
	return {};
}

bool MaterialWorkbench::isBusy() const
{
	// A playing preview always has a frame coming; that is not being busy.
	return m_scanning || (m_imageLane && m_imageLane->pending()) || (!m_preview->isPlaying() && m_preview->renderPending())
		|| m_reparseTimer->isActive();
}

bool MaterialWorkbench::hasUnsavedChanges() const
{
	return (m_document.kind != MaterialTextKind::None && m_editor->toPlainText() != m_document.savedText) || !m_buffers.isEmpty();
}

MaterialTextKind MaterialWorkbench::currentTextKind() const
{
	return m_document.kind;
}

QString MaterialWorkbench::currentText() const
{
	return m_editor->toPlainText();
}

bool MaterialWorkbench::selectMaterial(const QString& name)
{
	if (!m_library) {
		return false;
	}
	const int entry = m_library->indexOf(name);
	if (entry < 0) {
		return false;
	}
	const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(entry));
	if (proxyIndex.isValid()) {
		m_libraryView->setCurrentIndex(proxyIndex);
		m_libraryView->scrollTo(proxyIndex);
	}
	if (m_materialName != m_library->entries.at(entry).name) {
		selectEntry(entry);
	}
	return true;
}

void MaterialWorkbench::revealMaterial(const QString& name)
{
	const QString wanted = name.trimmed();
	if (wanted.isEmpty()) {
		return;
	}
	m_filter->clear();
	if (m_scanning || m_scanPending) {
		m_requestedName = wanted;
		return;
	}
	if (selectMaterial(wanted)) {
		return;
	}
	// Quake III maps name shaders without their textures/ folder.
	if (!wanted.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive) && selectMaterial(QStringLiteral("textures/") + wanted)) {
		return;
	}
	m_filter->setText(wanted);
	Q_EMIT statusMessage(tr("No material is named %1; showing the ones whose names contain it.").arg(wanted));
}

void MaterialWorkbench::showMaterialsUsingImage(const QString& image)
{
	// Materials list their images without extensions where the engine adds
	// one, so the path is matched without its own.
	QString stem = image.trimmed();
	const QString suffix = QFileInfo(stem).suffix();
	if (!suffix.isEmpty()) {
		stem.chop(suffix.size() + 1);
	}
	if (stem.isEmpty()) {
		return;
	}
	const QString term = stem.contains(QLatin1Char(' ')) ? QStringLiteral("image:\"%1\"").arg(stem) : QStringLiteral("image:%1").arg(stem);
	m_filter->setText(term);
	if (m_proxy->rowCount() > 0) {
		const QModelIndex first = m_proxy->index(0, 0);
		m_libraryView->setCurrentIndex(first);
		m_libraryView->scrollTo(first);
	}
	const int count = m_proxy->rowCount();
	Q_EMIT statusMessage(m_scanning || m_scanPending ? tr("Reading materials; the filter %1 applies when they are ready.").arg(term)
							 : count == 0 ? tr("No material reads %1.").arg(stem)
										  : tr("%n material(s) read %1.", nullptr, count).arg(stem));
}

MaterialWorkbench::Document MaterialWorkbench::documentFor(int entry, const MaterialDefinition& definition) const
{
	Document document;
	const MaterialLibraryEntry& item = m_library->entries.at(entry);
	document.engine = item.engine;
	if (item.scriptIndex >= 0 && item.scriptIndex < m_library->scripts.size()) {
		const MaterialScript& script = m_library->scripts.at(item.scriptIndex);
		document.kind = script.engine == MaterialEngine::Doom3 ? MaterialTextKind::Doom3Material : MaterialTextKind::Quake3Shader;
		document.engine = script.engine;
		document.savedText = script.text;
		if (item.scriptIndex == m_looseScript && !m_looseFile.isEmpty()) {
			document.filePath = m_looseFile;
			document.title = QDir::toNativeSeparators(m_looseFile);
		} else {
			document.virtualPath = script.path;
			document.title = script.path;
		}
		document.key = QStringLiteral("script:") + script.path.toLower();
		return document;
	}
	if ((item.kind == QStringLiteral("doom-wall") || item.kind == QStringLiteral("doom-flat")) && m_library->doomCatalog) {
		const DoomMaterialCatalog& catalog = *m_library->doomCatalog;
		if (catalog.hasAnimdefs) {
			document.kind = MaterialTextKind::DoomAnimdefs;
			document.savedText = catalog.animdefsText;
			document.virtualPath = catalog.animdefsPath.isEmpty() ? QStringLiteral("ANIMDEFS") : catalog.animdefsPath;
			document.title = document.virtualPath;
			document.key = QStringLiteral("animdefs:") + document.virtualPath.toLower();
		} else {
			document.kind = MaterialTextKind::DoomSwantbls;
			document.savedText = swantblsText(catalog.ranges, catalog.switches);
			document.virtualPath = QStringLiteral("ANIMATED");
			document.title = tr("Animations and switches (Boom SWANTBLS, saved as ANIMATED and SWITCHES)");
			document.key = QStringLiteral("swantbls");
		}
		return document;
	}
	if (item.kind == QStringLiteral("quake2-wal")) {
		QByteArray bytes;
		Quake2WalInfo info;
		if (readPackageFile(item.sourcePath, &bytes) && readQuake2WalInfo(bytes, &info)) {
			document.kind = MaterialTextKind::Quake2WalJson;
			document.savedText = quake2WalInfoText(info);
			document.walBytes = bytes;
			document.virtualPath = item.sourcePath;
			document.title = tr("%1 header (ericw-tools .wal_json)").arg(item.sourcePath);
			document.key = QStringLiteral("wal:") + item.sourcePath.toLower();
			return document;
		}
	}
	document.kind = MaterialTextKind::None;
	document.title = definition.sourcePath.isEmpty() ? definition.name : definition.sourcePath;
	document.key = QStringLiteral("none:") + materialEngineId(item.engine) + QLatin1Char(':') + definition.name.toLower();
	return document;
}

void MaterialWorkbench::selectEntry(int entry)
{
	if (!m_library || entry < 0 || entry >= m_library->entries.size()) {
		return;
	}
	const MaterialLibraryEntry& item = m_library->entries.at(entry);
	const MaterialDefinition definition = m_library->definition(entry);
	m_materialChangePending = m_materialChangePending || item.name != m_materialName || item.engine != m_engine;
	m_materialName = item.name;
	m_engine = item.engine;
	m_baseDefinition = definition;
	const Document document = documentFor(entry, definition);
	if (document.key != m_document.key) {
		switchDocument(document);
	} else {
		// The same document, read again (after a save or a rescan): take the
		// new saved text, and show it if nothing was typed since.
		const bool untouched = m_editor->toPlainText() == m_document.savedText;
		m_document = document;
		if (untouched && m_editor->toPlainText() != document.savedText) {
			setEditorText(document.savedText);
		}
	}
	reparse(true);
	Q_EMIT contentChanged();
}

void MaterialWorkbench::switchDocument(const Document& document)
{
	if (!m_document.key.isEmpty() && m_document.kind != MaterialTextKind::None) {
		const QString text = m_editor->toPlainText();
		if (text != m_document.savedText) {
			m_buffers.insert(m_document.key, text);
		} else {
			m_buffers.remove(m_document.key);
		}
	}
	m_document = document;
	m_highlighter->setTextKind(document.kind);
	m_highlighter->setDiagnostics({});
	const QString text = m_buffers.contains(document.key) ? m_buffers.take(document.key) : document.savedText;
	setEditorText(text);
	m_editor->setReadOnly(document.kind == MaterialTextKind::None);
	if (document.kind == MaterialTextKind::None) {
		m_editor->setPlaceholderText(m_engine == MaterialEngine::Quake
				? tr("Quake textures have no material text: the name sets animation (+0 to +9, +a to +j), liquids (*) and skies (sky).")
				: tr("The engine makes this material from the image alone. New Material writes a script for it."));
	} else {
		m_editor->setPlaceholderText(QString());
	}
	refreshDocumentState();
}

void MaterialWorkbench::setEditorText(const QString& text)
{
	m_applyingText = true;
	m_editor->setPlainText(text);
	m_applyingText = false;
}

void MaterialWorkbench::replaceEditorText(const QString& text)
{
	// One undo step changing only the part that differs, so the cursor and
	// the scroll position stay where they were.
	const QString current = m_editor->toPlainText();
	if (current == text) {
		return;
	}
	int prefix = 0;
	const int limit = static_cast<int>(std::min(current.size(), text.size()));
	while (prefix < limit && current.at(prefix) == text.at(prefix)) {
		++prefix;
	}
	int suffix = 0;
	while (suffix < limit - prefix && current.at(current.size() - 1 - suffix) == text.at(text.size() - 1 - suffix)) {
		++suffix;
	}
	QTextCursor cursor(m_editor->document());
	cursor.beginEditBlock();
	cursor.setPosition(prefix);
	cursor.setPosition(static_cast<int>(current.size()) - suffix, QTextCursor::KeepAnchor);
	cursor.insertText(text.mid(prefix, text.size() - prefix - suffix));
	cursor.endEditBlock();
}

void MaterialWorkbench::scheduleReparse()
{
	m_reparseTimer->start();
}

std::shared_ptr<const MaterialTableSet> MaterialWorkbench::tablesFor(const MaterialScript& script) const
{
	// The edited script's tables first, then the rest of the library's.
	auto tables = std::make_shared<MaterialTableSet>();
	tables->addAll(script.tables);
	if (m_library) {
		for (const QString& name : m_library->tables.names()) {
			if (const MaterialTable* table = m_library->tables.find(name)) {
				tables->add(*table);
			}
		}
	}
	tables->addStandIns();
	return tables;
}

void MaterialWorkbench::reparse(bool scrollToDefinition)
{
	m_reparseTimer->stop();
	const QString text = m_editor->toPlainText();
	m_parsedText = text;
	QVector<MaterialDiagnostic> diagnostics;
	m_definitionProblem.clear();
	MaterialDefinition definition = m_baseDefinition;
	std::shared_ptr<const MaterialTableSet> tables =
		m_library ? std::make_shared<const MaterialTableSet>(m_library->tables) : std::make_shared<const MaterialTableSet>();
	const bool flat = m_baseDefinition.kind == QStringLiteral("doom-flat");
	switch (m_document.kind) {
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material: {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		const MaterialScript script = parseMaterialScript(text, m_document.engine, path);
		diagnostics = script.allDiagnostics();
		if (const MaterialDefinition* found = script.find(m_materialName)) {
			definition = *found;
		} else if (!m_materialName.isEmpty()) {
			m_definitionProblem = tr("The text no longer defines %1; the preview shows the last version that did.").arg(m_materialName);
			definition = m_definition.name.isEmpty() ? m_baseDefinition : m_definition;
		}
		tables = tablesFor(script);
		break;
	}
	case MaterialTextKind::DoomSwantbls: {
		QVector<DoomAnimationRange> ranges;
		QVector<DoomSwitchPair> switches;
		parseSwantblsText(text, &ranges, &switches, &diagnostics);
		if (m_library && m_library->doomCatalog) {
			DoomMaterialCatalog catalog = *m_library->doomCatalog;
			catalog.ranges = ranges;
			catalog.switches = switches;
			catalog.animationSource = QStringLiteral("boom-animated");
			catalog.switchSource = QStringLiteral("boom-switches");
			definition = doomMaterialDefinition(catalog, m_materialName, flat);
		}
		break;
	}
	case MaterialTextKind::DoomAnimdefs: {
		const QVector<DoomAnimdefsEntry> entries = parseAnimdefs(text, &diagnostics);
		if (m_library && m_library->doomCatalog) {
			DoomMaterialCatalog catalog = *m_library->doomCatalog;
			catalog.animdefs = entries;
			catalog.hasAnimdefs = true;
			catalog.animdefsText = text;
			definition = doomMaterialDefinition(catalog, m_materialName, flat);
		}
		break;
	}
	case MaterialTextKind::Quake2WalJson: {
		Quake2WalInfo info;
		QString error;
		if (readQuake2WalInfo(m_document.walBytes, &info, &error) && parseQuake2WalInfoText(text, &info, &error)) {
			definition = quake2MaterialDefinition(m_materialName, info, [this](const QString& name, Quake2WalInfo* out) {
				QByteArray bytes;
				return readPackageFile(quake2WalPath(name), &bytes) && readQuake2WalInfo(bytes, out);
			});
		} else {
			MaterialDiagnostic problem = diagnosticOf(MaterialDiagnosticSeverity::Error, QStringLiteral("bad-metadata"), error);
			problem.line = 1;
			diagnostics.push_back(problem);
		}
		break;
	}
	case MaterialTextKind::None:
		break;
	}
	m_definition = definition;
	m_tables = tables;
	m_highlighter->setDiagnostics(diagnostics);
	updateProblems(diagnostics);
	const bool materialChanged = m_materialChangePending;
	m_materialChangePending = false;
	updateViews(materialChanged, scrollToDefinition);
}

void MaterialWorkbench::updateViews(bool materialChanged, bool scrollToDefinition)
{
	QString title = m_definition.name.isEmpty() ? m_materialName : m_definition.name;
	if (!title.isEmpty()) {
		title = tr("%1 · %2").arg(title, materialEngineDisplayName(m_definition.engine));
	}
	m_materialTitle->setText(title);
	updateGraph();
	resolveImages(materialChanged);
	updateDetails();
	if (materialChanged) {
		rebuildLightingMenu();
	}
	updateTimeline();
	updateNotes();
	if (scrollToDefinition && m_definition.span.line > 0 && m_document.kind != MaterialTextKind::None) {
		const QTextBlock block = m_editor->document()->findBlockByNumber(m_definition.span.line - 1);
		if (block.isValid()) {
			QTextCursor cursor(block);
			m_editor->setTextCursor(cursor);
			m_editor->centerCursor();
		}
	}
	refreshDocumentState();
}

void MaterialWorkbench::resolveImages(bool materialChanged)
{
	const QStringList references = m_definition.imageReferences();
	if (!materialChanged && m_images && references == m_imageReferences) {
		pushPreview(false);
		return;
	}
	const MaterialDefinition definition = m_definition;
	const MaterialImageSource source = imageSource();
	const std::shared_ptr<MaterialImageCache> cache = m_cache;
	updateNotes();
	m_imageLane->submit([this, definition, source, cache, references, materialChanged](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
		auto set = std::make_shared<MaterialImageSet>(resolveMaterialImages(definition, source, cancelled));
		if (set->cancelled) {
			return {};
		}
		QHash<QString, QImage> thumbnails;
		for (const QString& reference : references) {
			if (const MaterialTexturePtr texture = set->find(reference)) {
				if (texture->usable()) {
					thumbnails.insert(reference, texture->toImage().scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
				}
			}
		}
		return [this, set, references, thumbnails, materialChanged]() {
			m_images = set;
			m_imageReferences = references;
			m_graph->setThumbnails(thumbnails);
			updateImagesTab();
			pushPreview(materialChanged);
			updateNotes();
		};
	});
}

void MaterialWorkbench::pushPreview(bool materialChanged)
{
	if (m_definition.name.isEmpty() && m_materialName.isEmpty()) {
		return;
	}
	if (materialChanged && !m_shapeChosen) {
		const MaterialPreviewShape shape = defaultMaterialPreviewShape(m_definition);
		m_preview->setShape(shape);
		const int index = m_shape->findData(static_cast<int>(shape));
		if (index >= 0) {
			m_shape->setCurrentIndex(index);
		}
	}
	m_preview->setMaterial(m_definition, m_images, m_tables);
	if (materialChanged) {
		// Animated materials play by themselves unless motion is reduced;
		// still ones cost nothing.
		m_preview->setPlaying(m_definition.isAnimated() && !m_reducedMotion);
	}
}

void MaterialWorkbench::updateGraph()
{
	if (m_definition.name.isEmpty() && m_materialName.isEmpty()) {
		m_graph->clearGraph(tr("No material selected."));
		m_properties->setNode(nullptr, true);
		return;
	}
	const MaterialTextKind kind = m_document.kind == MaterialTextKind::None ? defaultMaterialTextKind(m_definition) : m_document.kind;
	const MaterialGraph graph = buildMaterialGraph(m_definition, kind);
	const bool readOnly = m_document.kind == MaterialTextKind::None;
	const QVector<MaterialGraphNodeTemplate> templates = readOnly ? QVector<MaterialGraphNodeTemplate>() : materialGraphNodeTemplates(m_definition.engine, kind);
	m_graph->setReadOnly(readOnly);
	m_graph->setNodeTemplates(templates);
	m_graph->setGraph(graph, m_materialName);
	m_properties->setNode(m_graph->graph().node(m_graph->selectedNode()), readOnly);
	rebuildAddNodeMenu(templates);
}

void MaterialWorkbench::rebuildAddNodeMenu(const QVector<MaterialGraphNodeTemplate>& templates)
{
	m_addNodeMenu->clear();
	QHash<QString, QMenu*> categories;
	for (const MaterialGraphNodeTemplate& each : templates) {
		QMenu* menu = categories.value(each.category);
		if (!menu) {
			menu = m_addNodeMenu->addMenu(each.category);
			menu->setToolTipsVisible(true);
			categories.insert(each.category, menu);
		}
		QAction* action = menu->addAction(each.title);
		action->setToolTip(each.description);
		const QString id = each.id;
		const bool needsStage = each.needsStage;
		connect(action, &QAction::triggered, this, [this, id, needsStage]() {
			const MaterialGraphNode* node = m_graph->graph().node(m_graph->selectedNode());
			MaterialGraphEdit edit;
			edit.kind = MaterialGraphEditKind::AddNode;
			edit.nodeTemplate = id;
			if (needsStage) {
				if (!node || node->stage < 0) {
					Q_EMIT statusMessage(tr("Select a stage, or a node inside one, first."));
					return;
				}
				edit.node = QStringLiteral("stage/%1").arg(node->stage);
			} else {
				edit.node = QStringLiteral("material");
			}
			applyGraphEdit(edit);
		});
	}
	m_addNode->setEnabled(!templates.isEmpty());
}

void MaterialWorkbench::updateProblems(const QVector<MaterialDiagnostic>& diagnostics)
{
	m_problems->clear();
	int count = 0;
	QVector<MaterialDiagnostic> all = diagnostics;
	if (!m_definition.engineRejection.isEmpty()) {
		bool explained = false;
		for (const MaterialDiagnostic& diagnostic : diagnostics) {
			explained = explained || diagnostic.message == m_definition.engineRejection;
		}
		if (!explained) {
			MaterialDiagnostic rejection = diagnosticOf(MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), m_definition.engineRejection);
			rejection.line = m_definition.span.line;
			all.push_back(rejection);
		}
	}
	if (!m_definitionProblem.isEmpty()) {
		all.push_back(diagnosticOf(MaterialDiagnosticSeverity::Warning, QStringLiteral("material-gone"), m_definitionProblem));
	}
	for (const MaterialDiagnostic& diagnostic : std::as_const(all)) {
		auto* item = new QTreeWidgetItem(m_problems);
		const QString icon = diagnostic.severity == MaterialDiagnosticSeverity::Error ? QStringLiteral("error")
			: diagnostic.severity == MaterialDiagnosticSeverity::Warning			  ? QStringLiteral("warning")
																					  : QStringLiteral("info");
		item->setIcon(0, studioIcon(icon));
		item->setText(0, severityName(diagnostic.severity));
		item->setText(1, diagnostic.line > 0 ? QString::number(diagnostic.line) : QString());
		QString message = diagnostic.message;
		if (!diagnostic.material.isEmpty() && materialLookupKey(diagnostic.material) != materialLookupKey(m_materialName)) {
			message = tr("%1: %2").arg(diagnostic.material, message);
		}
		item->setText(2, message);
		item->setToolTip(2, message);
		item->setData(0, Qt::UserRole, diagnostic.line);
		if (diagnostic.severity != MaterialDiagnosticSeverity::Info) {
			++count;
		}
	}
	m_problemCount = count;
	refreshTabTexts();
}

void MaterialWorkbench::updateImagesTab()
{
	m_imageList->clear();
	for (const QString& reference : m_definition.imageReferences()) {
		auto* item = new QTreeWidgetItem(m_imageList);
		item->setText(0, reference);
		item->setData(0, Qt::UserRole, reference);
		if (const MaterialTexturePtr texture = m_images ? m_images->find(reference) : nullptr) {
			item->setText(1, imageStatusName(texture->status));
			item->setText(2, texture->isValid() ? tr("%1 x %2").arg(texture->width).arg(texture->height) : QString());
			item->setText(3, texture->note.isEmpty() ? texture->path : (texture->path.isEmpty() ? texture->note : texture->path + QStringLiteral(" · ") + texture->note));
			item->setToolTip(3, texture->note);
			if (texture->usable()) {
				item->setIcon(0, QIcon(QPixmap::fromImage(texture->toImage().scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
			} else {
				item->setIcon(0, studioIcon(QStringLiteral("warning")));
			}
		} else if (const MaterialCubeTexturePtr cube = m_images ? m_images->findCube(reference) : nullptr) {
			item->setText(1, cube->isValid() ? tr("Cube map") : tr("Cube map with missing faces"));
			item->setText(3, cube->note);
			item->setIcon(0, studioIcon(cube->isValid() ? QStringLiteral("cube") : QStringLiteral("warning")));
		} else {
			item->setText(1, m_images ? imageStatusName(QStringLiteral("missing")) : tr("Reading..."));
			item->setIcon(0, studioIcon(m_images ? QStringLiteral("warning") : QStringLiteral("clock")));
		}
	}
	m_imageCount = static_cast<int>(m_definition.imageReferences().size());
	refreshTabTexts();
}

void MaterialWorkbench::updateDetails()
{
	if (m_definition.name.isEmpty()) {
		m_details->clear();
		return;
	}
	QStringList lines = materialDefinitionSummaryLines(m_definition);
	if (!m_definition.engineRejection.isEmpty()) {
		lines << QString() << tr("The engine drops this material: %1").arg(m_definition.engineRejection);
	}
	// What else in the package reads the same images.
	QStringList usedBy;
	if (m_library) {
		QSet<int> seen;
		for (const QString& reference : m_definition.imageReferences()) {
			for (int entry : m_library->entriesUsingImage(reference)) {
				if (!seen.contains(entry) && materialLookupKey(m_library->entries.at(entry).name) != materialLookupKey(m_definition.name)) {
					seen.insert(entry);
					usedBy << m_library->entries.at(entry).name;
				}
			}
		}
	}
	if (!usedBy.isEmpty()) {
		usedBy.sort(Qt::CaseInsensitive);
		const int shown = std::min(40, static_cast<int>(usedBy.size()));
		lines << QString() << tr("Materials sharing its images:");
		for (int index = 0; index < shown; ++index) {
			lines << QStringLiteral("  ") + usedBy.at(index);
		}
		if (usedBy.size() > shown) {
			const int more = static_cast<int>(usedBy.size()) - shown;
			lines << QStringLiteral("  ") + tr("and %n more", nullptr, more);
		}
	}
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
}

void MaterialWorkbench::updateNotes()
{
	QStringList notes;
	const MaterialRenderResult& result = m_preview->lastResult();
	if (!result.frameName.isEmpty()) {
		const int frames = static_cast<int>(m_definition.classic.frames.size());
		notes << (frames > 0 ? tr("Frame %1 (%2 of %3)").arg(result.frameName).arg(result.frameIndex + 1).arg(frames) : tr("Frame %1").arg(result.frameName));
	}
	if (result.fallback && !m_definition.engineRejection.isEmpty()) {
		notes << tr("The engine would not draw this material, so the preview shows what it draws instead: %1").arg(m_definition.engineRejection);
	}
	for (const QString& note : result.notes) {
		if (!notes.contains(note)) {
			notes << note;
		}
	}
	if (m_imageLane && m_imageLane->pending()) {
		notes << tr("Reading images...");
	} else if (m_images) {
		const int missing = static_cast<int>(m_images->missing().size());
		if (missing > 0) {
			notes << tr("%n image(s) not found; see Images.", nullptr, missing);
		}
		if (m_images->paletteGenerated && !m_images->palette.isEmpty()
			&& (m_definition.engine == MaterialEngine::Doom || m_definition.engine == MaterialEngine::Quake || m_definition.engine == MaterialEngine::Quake2)) {
			notes << tr("No game palette in the package: colours use a stand-in.");
		}
	}
	m_notes->setText(notes.join(QStringLiteral(" · ")));
	m_notes->setVisible(!notes.isEmpty());
}

double MaterialWorkbench::cycleSeconds() const
{
	double cycle = materialFramesCycleSeconds(m_definition.classic.frames);
	for (const MaterialStage& stage : m_definition.stages) {
		if (!stage.animationFrames.isEmpty() && stage.animationFrequency > 0.0) {
			cycle = std::max(cycle, stage.animationFrames.size() / stage.animationFrequency);
		}
	}
	return cycle > 0.0 ? std::clamp(cycle, 1.0, 60.0) : 10.0;
}

void MaterialWorkbench::updateTimeline()
{
	const double cycle = cycleSeconds();
	const double time = m_preview->time();
	if (!m_timeline->isSliderDown()) {
		const QSignalBlocker blocker(m_timeline);
		m_timeline->setValue(static_cast<int>(std::fmod(time, cycle) / cycle * 1000.0));
	}
	m_timeLabel->setText(tr("%1 s").arg(time, 0, 'f', 1));
	m_timeline->setAccessibleDescription(tr("%1 of a %2 second cycle").arg(std::fmod(time, cycle), 0, 'f', 1).arg(cycle, 0, 'f', 1));
}

void MaterialWorkbench::rebuildLightingMenu()
{
	m_lightingMenu->clear();
	const MaterialRenderOptions options = m_preview->renderOptions();
	const auto change = [this](const std::function<void(MaterialRenderOptions&)>& apply) {
		MaterialRenderOptions next = m_preview->renderOptions();
		apply(next);
		m_preview->setRenderOptions(next);
	};
	const auto toggle = [this, change](QMenu* menu, const QString& text, bool checked, const std::function<void(MaterialRenderOptions&, bool)>& apply) {
		QAction* action = menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(checked);
		connect(action, &QAction::toggled, this, [change, apply](bool on) { change([&](MaterialRenderOptions& next) { apply(next, on); }); });
		return action;
	};
	using Choice = std::pair<QString, std::function<void(MaterialRenderOptions&)>>;
	const auto choices = [this, change](QMenu* parent, const QString& title, const QVector<Choice>& items, int current) {
		QMenu* menu = parent->addMenu(title);
		auto* group = new QActionGroup(menu);
		group->setExclusive(true);
		for (int index = 0; index < items.size(); ++index) {
			QAction* action = menu->addAction(items.at(index).first);
			action->setCheckable(true);
			action->setChecked(index == current);
			group->addAction(action);
			const auto apply = items.at(index).second;
			connect(action, &QAction::triggered, this, [change, apply]() { change(apply); });
		}
		return menu;
	};
	const MaterialPreviewLighting& lighting = options.lighting;
	switch (m_definition.engine) {
	case MaterialEngine::Quake3: {
		toggle(m_lightingMenu, tr("Overbright Display"), lighting.overbright, [](MaterialRenderOptions& o, bool on) { o.lighting.overbright = on; });
		toggle(m_lightingMenu, tr("Lightmap Spot"), lighting.lightmapSpot, [](MaterialRenderOptions& o, bool on) { o.lighting.lightmapSpot = on; });
		const QVector<double> levels {0.5, 1.0, 1.5, 2.0};
		QVector<Choice> items;
		for (double level : levels) {
			items.push_back({tr("%1x").arg(level), [level](MaterialRenderOptions& o) { o.lighting.lightmap = level; }});
		}
		choices(m_lightingMenu, tr("Lightmap Brightness"), items,
			static_cast<int>(std::distance(levels.begin(), std::find(levels.begin(), levels.end(), lighting.lightmap))));
		choices(m_lightingMenu, tr("Surface"),
			{{tr("World Brush"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::World; }},
				{tr("Model"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::Model; }},
				{tr("2D Interface"), [](MaterialRenderOptions& o) { o.context = MaterialSurfaceContext::TwoD; }}},
			static_cast<int>(options.context));
		break;
	}
	case MaterialEngine::Doom: {
		const QVector<int> levels {255, 208, 160, 112, 64};
		QVector<Choice> items;
		for (int level : levels) {
			items.push_back({QString::number(level), [level](MaterialRenderOptions& o) { o.lighting.doomLight = level; }});
		}
		choices(m_lightingMenu, tr("Sector Light"), items,
			static_cast<int>(std::distance(levels.begin(), std::find(levels.begin(), levels.end(), lighting.doomLight))));
		toggle(m_lightingMenu, tr("Darken with Distance"), lighting.doomDistance, [](MaterialRenderOptions& o, bool on) { o.lighting.doomDistance = on; });
		toggle(m_lightingMenu, tr("Fake Contrast"), lighting.doomFakeContrast, [](MaterialRenderOptions& o, bool on) { o.lighting.doomFakeContrast = on; });
		toggle(m_lightingMenu, tr("Boom Texture Wrapping"), lighting.doomBoomWrapping, [](MaterialRenderOptions& o, bool on) { o.lighting.doomBoomWrapping = on; });
		if (!m_definition.classic.switchPartner.isEmpty()) {
			toggle(m_lightingMenu, tr("Switch Pressed"), options.alternate, [](MaterialRenderOptions& o, bool on) { o.alternate = on; });
		}
		break;
	}
	case MaterialEngine::Quake: {
		choices(m_lightingMenu, tr("Renderer"),
			{{tr("GLQuake"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::GLQuake; }},
				{tr("Modern Port"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::ModernPort; }},
				{tr("Software"), [](MaterialRenderOptions& o) { o.lighting.quakeRenderer = MaterialQuakeRenderer::Software; }}},
			static_cast<int>(lighting.quakeRenderer));
		QVector<Choice> styles;
		int currentStyle = -1;
		const QVector<QuakeLightStyle> known = quakeLightStyles();
		for (int index = 0; index < known.size(); ++index) {
			const QString pattern = known.at(index).pattern;
			if (pattern == lighting.quakeStyle) {
				currentStyle = index;
			}
			styles.push_back({tr("%1: %2").arg(known.at(index).number).arg(known.at(index).name), [pattern](MaterialRenderOptions& o) { o.lighting.quakeStyle = pattern; }});
		}
		choices(m_lightingMenu, tr("Light Style"), styles, currentStyle);
		if (!m_definition.classic.alternateFrames.isEmpty()) {
			toggle(m_lightingMenu, tr("Alternate Frames (+a)"), options.alternate, [](MaterialRenderOptions& o, bool on) { o.alternate = on; });
		}
		break;
	}
	case MaterialEngine::Quake2: {
		const QVector<double> intensities {1.0, 2.0, 3.0};
		QVector<Choice> items;
		for (double intensity : intensities) {
			items.push_back({QString::number(intensity), [intensity](MaterialRenderOptions& o) { o.lighting.quake2Intensity = intensity; }});
		}
		choices(m_lightingMenu, tr("Intensity"), items,
			static_cast<int>(std::distance(intensities.begin(), std::find(intensities.begin(), intensities.end(), lighting.quake2Intensity))));
		toggle(m_lightingMenu, tr("Lightmap Spot"), lighting.lightmapSpot, [](MaterialRenderOptions& o, bool on) { o.lighting.lightmapSpot = on; });
		break;
	}
	case MaterialEngine::Doom3: {
		choices(m_lightingMenu, tr("Interaction Shading"),
			{{tr("Doom 3"), [](MaterialRenderOptions& o) { o.lighting.doom3Shading = MaterialDoom3Shading::Vanilla; }},
				{tr("BFG Edition"), [](MaterialRenderOptions& o) { o.lighting.doom3Shading = MaterialDoom3Shading::Bfg; }}},
			static_cast<int>(lighting.doom3Shading));
		toggle(m_lightingMenu, tr("Light Orbits"), lighting.lightOrbit, [](MaterialRenderOptions& o, bool on) { o.lighting.lightOrbit = on; });
		toggle(m_lightingMenu, tr("Specular"), lighting.doom3Specular, [](MaterialRenderOptions& o, bool on) { o.lighting.doom3Specular = on; });
		const QVector<double> ambients {0.0, 0.1, 0.25};
		QVector<Choice> items;
		for (double ambient : ambients) {
			items.push_back({ambient == 0.0 ? tr("None") : tr("%1%").arg(ambient * 100.0), [ambient](MaterialRenderOptions& o) { o.lighting.doom3Ambient = ambient; }});
		}
		choices(m_lightingMenu, tr("Ambient Light"), items,
			static_cast<int>(std::distance(ambients.begin(), std::find(ambients.begin(), ambients.end(), lighting.doom3Ambient))));
		QAction* colour = m_lightingMenu->addAction(tr("Light Colour..."));
		connect(colour, &QAction::triggered, this, [this, change]() {
			const QColor chosen = QColorDialog::getColor(m_preview->renderOptions().lighting.lightColor, this, tr("Light Colour"));
			if (chosen.isValid()) {
				change([chosen](MaterialRenderOptions& o) { o.lighting.lightColor = chosen; });
			}
		});
		QAction* developer = m_lightingMenu->addAction(tr("Developer Default Image"));
		developer->setCheckable(true);
		developer->setChecked(m_developerDefault);
		developer->setToolTip(tr("Draw _default as the developer grid instead of the release build's transparent black."));
		connect(developer, &QAction::toggled, this, [this, change](bool on) {
			m_developerDefault = on;
			change([on](MaterialRenderOptions& o) { o.developerDefault = on; });
			resolveImages(true);
		});
		break;
	}
	case MaterialEngine::Unknown:
		break;
	}
	if (!m_lightingMenu->isEmpty()) {
		m_lightingMenu->addSeparator();
	}
	choices(m_lightingMenu, tr("Filtering"),
		{{tr("As the Engine Does"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Engine; }},
			{tr("Nearest"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Nearest; }},
			{tr("Bilinear"), [](MaterialRenderOptions& o) { o.filtering = MaterialFiltering::Bilinear; }}},
		static_cast<int>(options.filtering));
	toggle(m_lightingMenu, tr("Show Editor Image"), options.editorImage, [](MaterialRenderOptions& o, bool on) { o.editorImage = on; });
	toggle(m_lightingMenu, tr("Show What the Engine Draws Instead"), options.honourRejection,
		[](MaterialRenderOptions& o, bool on) { o.honourRejection = on; })
		->setToolTip(tr("When the engine would drop the material, draw its fallback (the default shader, _default) rather than the stages."));
	toggle(m_lightingMenu, tr("Checkerboard Behind"), options.checker, [](MaterialRenderOptions& o, bool on) { o.checker = on; });
	m_lightingMenu->setToolTipsVisible(true);
}

void MaterialWorkbench::showLine(int line)
{
	const QTextBlock block = m_editor->document()->findBlockByNumber(std::max(0, line - 1));
	if (!block.isValid()) {
		return;
	}
	QTextCursor cursor(block);
	m_editor->setTextCursor(cursor);
	m_editor->centerCursor();
	m_editor->setFocus(Qt::OtherFocusReason);
}

void MaterialWorkbench::refreshDocumentState()
{
	if (!m_editor || !m_save) {
		return;
	}
	const bool hasText = m_document.kind != MaterialTextKind::None;
	const bool modified = hasText && m_editor->toPlainText() != m_document.savedText;
	QString label = m_document.title;
	if (modified) {
		label = tr("%1 (modified)").arg(label);
	}
	if (!m_buffers.isEmpty()) {
		const int others = static_cast<int>(m_buffers.size());
		label += QStringLiteral(" · ") + tr("%n other text(s) with unsaved changes", nullptr, others);
	}
	m_documentLabel->setText(label);
	m_documentLabel->setAccessibleName(label);
	const bool canStage = static_cast<bool>(m_host.stageFile) || static_cast<bool>(m_host.stageLump);
	const bool canSave = hasText && (!m_document.filePath.isEmpty() || (m_reader && canStage) || m_document.virtualPath.isEmpty());
	m_save->setEnabled(modified && canSave);
	m_save->setToolTip(!m_document.filePath.isEmpty() ? tr("Save to %1 (Ctrl+S).").arg(QDir::toNativeSeparators(m_document.filePath))
		: m_document.virtualPath.isEmpty()			 ? tr("Save the new script to a file (Ctrl+S).")
													 : tr("Stage the change into the open package (Ctrl+S); saving the package publishes it."));
	m_revert->setEnabled(modified);
	m_newMaterial->setEnabled(true);
	const bool unsaved = hasUnsavedChanges();
	if (unsaved != m_lastModified) {
		m_lastModified = unsaved;
		Q_EMIT unsavedChangesChanged(unsaved);
		Q_EMIT contentChanged();
	}
}

// --- Editing -----------------------------------------------------------------

bool MaterialWorkbench::applyGraphEdit(const MaterialGraphEdit& requested, QString* error)
{
	QString problem;
	if (m_document.kind == MaterialTextKind::None) {
		problem = tr("This material has no text to edit.");
	}
	MaterialGraphEdit edit = requested;
	if (edit.related.isEmpty()) {
		edit.related << m_materialName;
		for (const MaterialFrame& frame : m_definition.classic.frames) {
			edit.related << frame.name;
		}
	}
	if (edit.kind == MaterialGraphEditKind::AddNode && edit.nodeTemplate == QStringLiteral("doom.range") && edit.value.isEmpty()) {
		edit.value = m_baseDefinition.kind == QStringLiteral("doom-flat") ? QStringLiteral("flat") : QStringLiteral("texture");
	}
	MaterialGraphEditResult result;
	if (problem.isEmpty()) {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		result = applyMaterialGraphEdit(m_editor->toPlainText(), m_document.kind, m_materialName, edit, path);
		if (!result.ok) {
			problem = result.error;
		}
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		m_notes->setText(problem);
		m_notes->setVisible(true);
		return false;
	}
	replaceEditorText(result.text);
	reparse(false);
	if (!result.focusNode.isEmpty()) {
		m_graph->selectNode(result.focusNode);
	}
	return true;
}

bool MaterialWorkbench::addMaterialFromTemplate(const QString& templateId, const QString& name, const QString& image, QString* error)
{
	MaterialTemplate materialTemplate;
	QString problem;
	const QString trimmed = name.trimmed();
	if (!materialTemplateById(templateId, &materialTemplate)) {
		problem = tr("Unknown template %1.").arg(templateId);
	} else if (!plainMaterialName(trimmed)) {
		problem = tr("A material name cannot hold spaces, braces or quotes.");
	}
	const MaterialTextKind kind = materialTemplate.engine == MaterialEngine::Doom3 ? MaterialTextKind::Doom3Material : MaterialTextKind::Quake3Shader;
	const QString body = problem.isEmpty() ? instantiateMaterialTemplate(materialTemplate, trimmed, image) : QString();
	if (problem.isEmpty() && m_document.kind == kind) {
		const QString path = m_document.virtualPath.isEmpty() ? m_document.filePath : m_document.virtualPath;
		const MaterialScript script = parseMaterialScript(m_editor->toPlainText(), materialTemplate.engine, path);
		if (script.find(trimmed)) {
			problem = tr("This script already defines %1.").arg(trimmed);
		} else {
			MaterialEdit edit;
			edit.kind = MaterialEditKind::AddDefinition;
			edit.text = body;
			const MaterialEditResult result = applyMaterialEdit(script, edit);
			if (!result.ok) {
				problem = result.error;
			} else {
				m_materialName = trimmed;
				m_engine = materialTemplate.engine;
				m_baseDefinition = MaterialDefinition();
				m_materialChangePending = true;
				replaceEditorText(result.text);
				reparse(true);
			}
		}
	} else if (problem.isEmpty()) {
		// A new script: staged into the package on save, or saved as a file.
		Document document;
		document.kind = kind;
		document.engine = materialTemplate.engine;
		if (m_reader) {
			document.virtualPath = freeScriptPath(materialTemplate.engine);
			document.title = tr("%1 (new)").arg(document.virtualPath);
		} else {
			document.title = tr("New script (not saved)");
		}
		document.key = QStringLiteral("new:") + document.virtualPath + QLatin1Char(':') + trimmed.toLower();
		switchDocument(document);
		m_materialName = trimmed;
		m_engine = materialTemplate.engine;
		m_baseDefinition = MaterialDefinition();
		m_materialChangePending = true;
		replaceEditorText(body + QLatin1Char('\n'));
		reparse(true);
	}
	if (!problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	Q_EMIT statusMessage(tr("Added %1; save to keep it.").arg(trimmed));
	return true;
}

void MaterialWorkbench::showNewMaterialDialog()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New Material"));
	dialog.setAccessibleName(tr("New material"));
	auto* form = new QFormLayout;
	auto* templates = new QComboBox(&dialog);
	templates->setAccessibleName(tr("Template"));
	MaterialEngine preferred = m_document.engine;
	if (preferred != MaterialEngine::Quake3 && preferred != MaterialEngine::Doom3) {
		preferred = m_library && m_library->count(MaterialEngine::Doom3) > m_library->count(MaterialEngine::Quake3) ? MaterialEngine::Doom3
																													: MaterialEngine::Quake3;
	}
	auto* description = new QLabel(&dialog);
	description->setWordWrap(true);
	description->setForegroundRole(QPalette::PlaceholderText);
	for (const MaterialTemplate& each : materialTemplates()) {
		templates->addItem(tr("%1: %2").arg(materialEngineDisplayName(each.engine), each.displayName), each.id);
		templates->setItemData(templates->count() - 1, each.description, Qt::ToolTipRole);
		if (each.engine == preferred && templates->count() > 0 && templates->currentData().toString().isEmpty()) {
			templates->setCurrentIndex(templates->count() - 1);
		}
	}
	const auto describe = [templates, description]() {
		MaterialTemplate chosen;
		if (materialTemplateById(templates->currentData().toString(), &chosen)) {
			description->setText(chosen.description);
		}
	};
	connect(templates, &QComboBox::currentIndexChanged, &dialog, describe);
	for (int index = 0; index < templates->count(); ++index) {
		MaterialTemplate each;
		if (materialTemplateById(templates->itemData(index).toString(), &each) && each.engine == preferred) {
			templates->setCurrentIndex(index);
			break;
		}
	}
	describe();
	auto* name = new QLineEdit(&dialog);
	name->setAccessibleName(tr("Material name"));
	const QString folder = m_materialName.contains(QLatin1Char('/')) ? m_materialName.section(QLatin1Char('/'), 0, -2) : QStringLiteral("textures/custom");
	name->setText(folder + QStringLiteral("/new_material"));
	auto* image = new QLineEdit(&dialog);
	image->setAccessibleName(tr("Main image"));
	image->setPlaceholderText(tr("The material's name"));
	const QStringList images = packageImages();
	if (!images.isEmpty()) {
		auto* completer = new QCompleter(images, image);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		completer->setFilterMode(Qt::MatchContains);
		image->setCompleter(completer);
	}
	form->addRow(tr("Template"), templates);
	form->addRow(QString(), description);
	form->addRow(tr("Name"), name);
	form->addRow(tr("Image"), image);
	auto* problem = new QLabel(&dialog);
	problem->setWordWrap(true);
	problem->setVisible(false);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(problem);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		QString error;
		if (addMaterialFromTemplate(templates->currentData().toString(), name->text(), image->text(), &error)) {
			dialog.accept();
		} else {
			problem->setText(error);
			problem->setVisible(true);
			problem->setAccessibleName(error);
		}
	});
	name->setFocus();
	name->selectAll();
	dialog.exec();
}

bool MaterialWorkbench::writeLooseFile(const QString& path, const QByteArray& bytes, QString* error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	return true;
}

bool MaterialWorkbench::save(QString* error)
{
	QString problem;
	const QString text = m_editor->toPlainText();
	bool ok = false;
	switch (m_document.kind) {
	case MaterialTextKind::None:
		problem = tr("This material has no text to save.");
		break;
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material:
		if (!m_document.filePath.isEmpty()) {
			ok = writeLooseFile(m_document.filePath, text.toUtf8(), &problem);
		} else if (!m_document.virtualPath.isEmpty() && m_reader && m_host.stageFile) {
			ok = m_host.stageFile(text.toUtf8(), m_document.virtualPath, &problem);
		} else {
			problem = tr("There is no package to stage this script into; use Save As.");
		}
		break;
	case MaterialTextKind::DoomSwantbls: {
		QVector<DoomAnimationRange> ranges;
		QVector<DoomSwitchPair> switches;
		QVector<MaterialDiagnostic> diagnostics;
		if (!parseSwantblsText(text, &ranges, &switches, &diagnostics)) {
			problem = diagnostics.isEmpty() ? tr("The table text has errors.") : tr("Line %1: %2").arg(diagnostics.first().line).arg(diagnostics.first().message);
			break;
		}
		const QByteArray animated = boomAnimatedLump(ranges);
		const QByteArray switchLump = boomSwitchesLump(switches);
		if (packageIsWad() && m_host.stageLump) {
			ok = m_host.stageLump(animated, QStringLiteral("ANIMATED"), &problem) && m_host.stageLump(switchLump, QStringLiteral("SWITCHES"), &problem);
		} else if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(animated, QStringLiteral("ANIMATED.lmp"), &problem) && m_host.stageFile(switchLump, QStringLiteral("SWITCHES.lmp"), &problem);
		} else {
			problem = tr("Open the WAD or package the tables belong to.");
		}
		break;
	}
	case MaterialTextKind::DoomAnimdefs:
		if (packageIsWad() && m_host.stageLump) {
			ok = m_host.stageLump(text.toUtf8(), QStringLiteral("ANIMDEFS"), &problem);
		} else if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(text.toUtf8(), m_document.virtualPath, &problem);
		} else {
			problem = tr("Open the WAD or package ANIMDEFS belongs to.");
		}
		break;
	case MaterialTextKind::Quake2WalJson: {
		Quake2WalInfo info;
		QByteArray bytes = m_document.walBytes;
		if (!readQuake2WalInfo(bytes, &info, &problem) || !parseQuake2WalInfoText(text, &info, &problem) || !rewriteQuake2WalHeader(&bytes, info, &problem)) {
			break;
		}
		if (m_reader && m_host.stageFile) {
			ok = m_host.stageFile(bytes, m_document.virtualPath, &problem);
			if (ok) {
				m_document.walBytes = bytes;
			}
		} else {
			problem = tr("Open the package the WAL belongs to.");
		}
		break;
	}
	}
	if (!ok) {
		if (problem.isEmpty()) {
			problem = tr("Saving is not available here.");
		}
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	m_document.savedText = text;
	m_buffers.remove(m_document.key);
	refreshDocumentState();
	if (!m_document.filePath.isEmpty() && m_document.filePath == m_looseFile) {
		// A loose script: read it again so the library shows what was saved.
		startScan();
	}
	Q_EMIT statusMessage(m_document.filePath.isEmpty() ? tr("Staged %1 into the package; save the package to publish it.").arg(m_document.title)
														: tr("Saved %1.").arg(QDir::toNativeSeparators(m_document.filePath)));
	return true;
}

bool MaterialWorkbench::saveAs(const QString& path, QString* error)
{
	if (m_document.kind != MaterialTextKind::Quake3Shader && m_document.kind != MaterialTextKind::Doom3Material) {
		if (error) {
			*error = tr("Only scripts can be saved to a file of their own.");
		}
		return false;
	}
	QString problem;
	if (!writeLooseFile(path, m_editor->toPlainText().toUtf8(), &problem)) {
		if (error) {
			*error = problem;
		}
		Q_EMIT statusMessage(problem);
		return false;
	}
	const QFileInfo info(path);
	m_buffers.remove(m_document.key);
	m_document.filePath = info.absoluteFilePath();
	m_document.virtualPath.clear();
	m_document.title = QDir::toNativeSeparators(info.absoluteFilePath());
	m_document.savedText = m_editor->toPlainText();
	m_document.key = QStringLiteral("file:") + info.absoluteFilePath().toLower();
	m_looseFile = info.absoluteFilePath();
	refreshDocumentState();
	startScan();
	Q_EMIT statusMessage(tr("Saved %1.").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
	return true;
}

void MaterialWorkbench::chooseSaveAs()
{
	const bool doom3 = m_document.kind == MaterialTextKind::Doom3Material;
	const QString filter = doom3 ? tr("Doom 3 materials (*.mtr)") : tr("Quake III shaders (*.shader)");
	const QString suggested = m_document.virtualPath.isEmpty() ? (doom3 ? QStringLiteral("vibestudio.mtr") : QStringLiteral("vibestudio.shader"))
															   : QFileInfo(m_document.virtualPath).fileName();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Material Script"), suggested, filter);
	if (!path.isEmpty()) {
		saveAs(path);
	}
}

void MaterialWorkbench::revert()
{
	if (m_document.kind == MaterialTextKind::None) {
		return;
	}
	replaceEditorText(m_document.savedText);
	reparse(false);
}

} // namespace vibestudio
