#include "app/level_generation_dialog.h"

#include "core/level_mapgen.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <atomic>

namespace vibestudio {

struct LevelGenerationDialog::Work {
	std::atomic_bool discard = false;
	// Stops a Quake-MapGen text model waiting for its answer.
	std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
	QThread* thread = nullptr;
	LevelGenerationResult result;
};

namespace {

QComboBox* choiceBox(const QString& accessibleName, const QVector<QPair<QString, QString>>& items)
{
	auto* box = new QComboBox;
	box->setAccessibleName(accessibleName);
	for (const auto& item : items) {
		box->addItem(item.first, item.second);
	}
	return box;
}

QString choice(const QComboBox* box)
{
	return box ? box->currentData().toString() : QString();
}

// Enables or disables the item holding `data`, with why when it is off.
void setChoiceEnabled(QComboBox* box, const QString& data, bool enabled, const QString& whyNot)
{
	auto* model = qobject_cast<QStandardItemModel*>(box->model());
	const int index = box->findData(data);
	if (!model || index < 0) {
		return;
	}
	QStandardItem* item = model->item(index);
	item->setEnabled(enabled);
	item->setToolTip(enabled ? QString() : whyNot);
}

QPlainTextEdit* readOnlyText(const QString& objectName, const QString& accessibleName)
{
	auto* text = new QPlainTextEdit;
	text->setObjectName(objectName);
	text->setAccessibleName(accessibleName);
	text->setReadOnly(true);
	text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	return text;
}

} // namespace

LevelGenerationDialog::LevelGenerationDialog(QWidget* parent, LevelGenerationDialogHooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
	, m_work(std::make_shared<Work>())
	, m_client(std::make_unique<AiChatClient>())
{
	setObjectName(QStringLiteral("levelGenerationDialog"));
	setWindowTitle(tr("Generate Level"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Describe a level, choose who plans it, review the layout, then open it in Levels or save it."));
	resize(1120, 740);

	auto* root = new QVBoxLayout(this);
	auto* splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	root->addWidget(splitter, 1);

	// The brief and the options.
	auto* form = new QWidget;
	auto* formLayout = new QVBoxLayout(form);
	formLayout->setContentsMargins(0, 0, 8, 0);
	auto* promptLabel = new QLabel(tr("&Describe the level"));
	m_prompt = new QPlainTextEdit;
	m_prompt->setObjectName(QStringLiteral("levelGenerationPrompt"));
	m_prompt->setAccessibleName(tr("Level description"));
	m_prompt->setPlaceholderText(tr("For example: a gothic castle with lava pits, 8 rooms, high verticality"));
	m_prompt->setTabChangesFocus(true);
	m_prompt->setMaximumHeight(110);
	promptLabel->setBuddy(m_prompt);
	formLayout->addWidget(promptLabel);
	formLayout->addWidget(m_prompt);

	auto* options = new QFormLayout;
	m_options = options;
	options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_generator = choiceBox(tr("Generator"), {{tr("Studio blockout"), QStringLiteral("studio")}, {tr("Quake-MapGen"), levelMapgenGeneratorId()}});
	m_generator->setObjectName(QStringLiteral("levelGenerationGenerator"));
	m_generator->setItemData(0, tr("Rooms and corridors on a grid of sealed cells, with themed textures, liquids, and monsters. Quake, Quake II, Quake III Arena, and Doom."),
		Qt::ToolTipRole);
	m_generator->setItemData(1, tr("Quake-MapGen: ranked layouts with stairs, raised crossings, and passages, checked for navigation, visibility, and lighting before the map is "
								   "written. Quake, Quake II, and Quake III Arena, with capture the flag. It reads the theme from the description."),
		Qt::ToolTipRole);
	m_generator->setToolTip(tr("Which generator builds the level. Hover over each for what it makes."));
	options->addRow(tr("Ge&nerator:"), m_generator);
	// The open map's or installation's game when there is one; otherwise the description names it.
	m_game = choiceBox(tr("Game"), {{tr("From the description"), QString()}, {tr("Quake"), QStringLiteral("quake")}, {tr("Quake II"), QStringLiteral("quake2")},
								   {tr("Quake III Arena"), QStringLiteral("quake3")}, {tr("Doom"), QStringLiteral("doom")}});
	m_game->setObjectName(QStringLiteral("levelGenerationGame"));
	const QString game = m_hooks.defaultGame ? m_hooks.defaultGame() : QString();
	if (const int index = m_game->findData(game); index >= 0) {
		m_game->setCurrentIndex(index);
	}
	options->addRow(tr("&Game:"), m_game);
	m_mode = choiceBox(tr("Mode"), {{tr("From the description"), QString()}, {tr("Single player"), QStringLiteral("single-player")},
								   {tr("Deathmatch"), QStringLiteral("deathmatch")}, {tr("Duel"), QStringLiteral("duel")}, {tr("Capture the flag"), QStringLiteral("ctf")}});
	options->addRow(tr("&Mode:"), m_mode);
	m_format = choiceBox(tr("Map format"), {});
	m_format->setObjectName(QStringLiteral("levelGenerationFormat"));
	m_format->setToolTip(tr("How the map file writes its brushes. Quake III Arena takes classic brushes or brush primitives."));
	options->addRow(tr("Map &format:"), m_format);
	m_theme = choiceBox(tr("Theme"), {{tr("From the description"), QString()}, {tr("Base"), QStringLiteral("base")}, {tr("Medieval"), QStringLiteral("medieval")},
									 {tr("Metal"), QStringLiteral("metal")}, {tr("Hell"), QStringLiteral("hell")}, {tr("Cave"), QStringLiteral("cave")}});
	options->addRow(tr("&Theme:"), m_theme);
	m_rooms = new QSpinBox;
	m_rooms->setAccessibleName(tr("Room count"));
	m_rooms->setRange(0, 16);
	m_rooms->setSpecialValueText(tr("From the description"));
	options->addRow(tr("&Rooms:"), m_rooms);
	m_verticality = choiceBox(tr("Verticality"), {{tr("From the description"), QString()}, {tr("Flat"), QStringLiteral("flat")}, {tr("Low"), QStringLiteral("low")},
												 {tr("Medium"), QStringLiteral("medium")}, {tr("High"), QStringLiteral("high")}});
	options->addRow(tr("&Verticality:"), m_verticality);
	m_intensity = choiceBox(tr("Intensity"), {{tr("From the description"), QString()}, {tr("Quiet"), QStringLiteral("quiet")}, {tr("Balanced"), QStringLiteral("balanced")},
												 {tr("Intense"), QStringLiteral("intense")}});
	m_intensity->setToolTip(tr("How much combat the plan asks for: fewer encounters, or more."));
	options->addRow(tr("&Intensity:"), m_intensity);
	m_liquid = choiceBox(tr("Liquid in pits"), {{tr("From the description"), QString()}, {tr("Water"), QStringLiteral("water")}, {tr("Slime"), QStringLiteral("slime")},
											   {tr("Lava"), QStringLiteral("lava")}});
	options->addRow(tr("&Liquid:"), m_liquid);
	m_monsters = choiceBox(tr("Monsters"), {{tr("From the description"), QString()}, {tr("None"), QStringLiteral("none")}, {tr("Light"), QStringLiteral("light")},
										   {tr("Normal"), QStringLiteral("normal")}, {tr("Heavy"), QStringLiteral("heavy")}});
	options->addRow(tr("M&onsters:"), m_monsters);
	m_players = new QSpinBox;
	m_players->setAccessibleName(tr("Players"));
	m_players->setRange(0, 32);
	m_players->setSpecialValueText(tr("From the description"));
	options->addRow(tr("&Players:"), m_players);
	auto* seedRow = new QHBoxLayout;
	m_seedFromPrompt = new QCheckBox(tr("From the description"));
	m_seedFromPrompt->setAccessibleName(tr("Seed from the description"));
	m_seedFromPrompt->setToolTip(tr("The same description always gives the same level. Untick to choose a seed."));
	m_seedFromPrompt->setChecked(true);
	m_seed = new QSpinBox;
	m_seed->setAccessibleName(tr("Seed"));
	m_seed->setRange(0, 999999999);
	m_seed->setEnabled(false);
	m_newSeed = new QPushButton(tr("New &Seed"));
	m_newSeed->setAccessibleName(tr("Generate with a new seed"));
	m_newSeed->setToolTip(tr("Pick a new seed and generate again: the same plan, laid out another way."));
	seedRow->addWidget(m_seedFromPrompt);
	seedRow->addWidget(m_seed, 1);
	seedRow->addWidget(m_newSeed);
	options->addRow(tr("Seed:"), seedRow);
	formLayout->addLayout(options);
	m_projectTextures = new QCheckBox(tr("Use the project's &textures where they fit"));
	m_projectTextures->setAccessibleName(m_projectTextures->text().remove(QLatin1Char('&')));
	m_projectTextures->setToolTip(tr("Match walls, floors, ceilings, and liquids to textures the open package and project have, instead of the game's stock names."));
	m_projectTextures->setChecked(true);
	formLayout->addWidget(m_projectTextures);

	auto* planner = new QGroupBox(tr("Who plans the rooms"));
	auto* plannerLayout = new QVBoxLayout(planner);
	m_rulesPlanner = new QRadioButton(tr("The &rules, on this machine"));
	m_rulesPlanner->setObjectName(QStringLiteral("levelGenerationRulesPlanner"));
	m_rulesPlanner->setAccessibleName(tr("Plan with the rules"));
	m_modelPlanner = new QRadioButton(tr("The &text model"));
	m_modelPlanner->setObjectName(QStringLiteral("levelGenerationModelPlanner"));
	m_modelPlanner->setAccessibleName(tr("Plan with the text model"));
	m_rulesPlanner->setChecked(true);
	m_plannerStatus = new QLabel;
	m_plannerStatus->setObjectName(QStringLiteral("levelGenerationPlannerStatus"));
	m_plannerStatus->setWordWrap(true);
	m_plannerStatus->setTextFormat(Qt::PlainText);
	m_previewRequest = new QPushButton(tr("Preview Re&quest…"));
	m_previewRequest->setAccessibleName(tr("Preview the plan request"));
	m_previewRequest->setToolTip(tr("Show exactly what would be sent to the text model, without sending it."));
	m_aiSettings = new QPushButton(tr("AI Settings…"));
	m_aiSettings->setObjectName(QStringLiteral("levelGenerationAiSettings"));
	m_aiSettings->setAccessibleName(tr("Open AI settings"));
	m_aiSettings->setToolTip(tr("Choose the connector, model, and endpoint in Settings > AI and Automation."));
	connect(m_aiSettings, &QPushButton::clicked, this, [this] {
		if (m_hooks.openAiSettings) {
			m_hooks.openAiSettings();
		}
	});
	plannerLayout->addWidget(m_rulesPlanner);
	plannerLayout->addWidget(m_modelPlanner);
	plannerLayout->addWidget(m_plannerStatus);
	auto* modelButtons = new QHBoxLayout;
	modelButtons->addWidget(m_previewRequest);
	modelButtons->addWidget(m_aiSettings);
	modelButtons->addStretch(1);
	plannerLayout->addLayout(modelButtons);
	formLayout->addWidget(planner);

	auto* actions = new QHBoxLayout;
	m_generate = new QPushButton(tr("&Generate"));
	m_generate->setObjectName(QStringLiteral("levelGenerationGenerate"));
	m_generate->setAccessibleName(tr("Generate the level"));
	m_generate->setToolTip(tr("Plan, lay out, and build the level (Ctrl+Enter)."));
	m_generate->setDefault(true);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName(QStringLiteral("levelGenerationCancel"));
	m_cancel->setAccessibleName(tr("Cancel generation"));
	m_cancel->setEnabled(false);
	actions->addWidget(m_generate);
	actions->addWidget(m_cancel);
	actions->addStretch(1);
	formLayout->addLayout(actions);
	m_status = new QLabel(tr("Describe the level and press Generate."));
	m_status->setObjectName(QStringLiteral("levelGenerationStatus"));
	m_status->setAccessibleName(tr("Generation status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Generation progress"));
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	formLayout->addWidget(m_status);
	formLayout->addWidget(m_progress);
	formLayout->addStretch(1);
	splitter->addWidget(form);

	// The result.
	auto* review = new QWidget;
	auto* reviewLayout = new QVBoxLayout(review);
	reviewLayout->setContentsMargins(8, 0, 0, 0);
	m_previewImage = new QLabel;
	m_previewImage->setObjectName(QStringLiteral("levelGenerationPreview"));
	m_previewImage->setAccessibleName(tr("Level layout preview"));
	m_previewImage->setAlignment(Qt::AlignCenter);
	m_previewImage->setMinimumSize(320, 320);
	m_previewImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_previewImage->setText(tr("The layout appears here: north is up, floors are lighter where higher."));
	m_previewImage->setWordWrap(true);
	reviewLayout->addWidget(m_previewImage, 1);
	m_legend = new QLabel(tr("▲ start or spawn (pointing where it faces)   ● monster   ▬ weapon   ■ item   ◆ exit   · light   Pools: lava, slime, water"));
	m_legend->setAccessibleName(tr("Preview legend"));
	m_legend->setWordWrap(true);
	reviewLayout->addWidget(m_legend);
	m_summary = new QLabel;
	m_summary->setObjectName(QStringLiteral("levelGenerationSummary"));
	m_summary->setAccessibleName(tr("Level summary"));
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	reviewLayout->addWidget(m_summary);
	m_details = new QToolButton;
	m_details->setText(tr("&Details"));
	m_details->setAccessibleName(tr("Show the plan, notes, and map text"));
	m_details->setCheckable(true);
	m_details->setArrowType(Qt::RightArrow);
	m_details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	reviewLayout->addWidget(m_details, 0, Qt::AlignLeft);
	m_detailTabs = new QTabWidget;
	m_detailTabs->setAccessibleName(tr("Generation details"));
	m_planText = readOnlyText(QStringLiteral("levelGenerationPlan"), tr("Level plan"));
	m_notesText = readOnlyText(QStringLiteral("levelGenerationNotes"), tr("Notes and warnings"));
	m_mapText = readOnlyText(QStringLiteral("levelGenerationMapText"), tr("Generated map text"));
	m_mapText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_mapText->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_analysisText = readOnlyText(QStringLiteral("levelGenerationAnalysis"), tr("Navigation, visibility, lighting, and quality analysis"));
	m_reportText = readOnlyText(QStringLiteral("levelGenerationReport"), tr("Quake-MapGen report"));
	m_reportText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_reportText->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_detailTabs->addTab(m_planText, tr("Plan"));
	m_detailTabs->addTab(m_notesText, tr("Notes"));
	m_detailTabs->addTab(m_analysisText, tr("Analysis"));
	m_detailTabs->addTab(m_mapText, tr("Map"));
	m_detailTabs->addTab(m_reportText, tr("Report"));
	// Quake-MapGen's own tabs appear with its results.
	m_detailTabs->setTabVisible(m_detailTabs->indexOf(m_analysisText), false);
	m_detailTabs->setTabVisible(m_detailTabs->indexOf(m_reportText), false);
	m_detailTabs->setVisible(false);
	reviewLayout->addWidget(m_detailTabs, 1);
	splitter->addWidget(review);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({380, 740});

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_open = buttons->addButton(tr("&Open in Levels"), QDialogButtonBox::ActionRole);
	m_open->setObjectName(QStringLiteral("levelGenerationOpen"));
	m_open->setAccessibleName(tr("Open the level in the Levels editor"));
	m_open->setToolTip(tr("Open it as a new, unsaved map; Save As gives it a file."));
	m_save = buttons->addButton(tr("Save &As…"), QDialogButtonBox::ActionRole);
	m_save->setAccessibleName(tr("Save the generated level"));
	m_savePlan = buttons->addButton(tr("Save &Plan…"), QDialogButtonBox::ActionRole);
	m_savePlan->setAccessibleName(tr("Save the level plan as JSON"));
	m_savePlan->setToolTip(tr("Keep the plan to edit by hand or build again: vibestudio --cli map generate --plan."));
	m_saveReport = buttons->addButton(tr("Save R&eport…"), QDialogButtonBox::ActionRole);
	m_saveReport->setAccessibleName(tr("Save the Quake-MapGen report as JSON"));
	m_saveReport->setToolTip(tr("Keep the plan, ranking, repairs, and analysis Quake-MapGen recorded, as its .qmg.json report."));
	for (QPushButton* button : {m_open, m_save, m_savePlan, m_saveReport}) {
		button->setEnabled(false);
	}
	m_saveReport->setVisible(false);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &LevelGenerationDialog::reject);
	connect(m_generate, &QPushButton::clicked, this, &LevelGenerationDialog::generate);
	connect(m_cancel, &QPushButton::clicked, this, &LevelGenerationDialog::cancelGeneration);
	connect(m_newSeed, &QPushButton::clicked, this, [this] {
		m_seedFromPrompt->setChecked(false);
		m_seed->setValue(int(QRandomGenerator::global()->bounded(1000000000u)));
		generate();
	});
	connect(m_seedFromPrompt, &QCheckBox::toggled, m_seed, [this](bool fromPrompt) { m_seed->setEnabled(!fromPrompt); });
	connect(m_rulesPlanner, &QRadioButton::toggled, this, &LevelGenerationDialog::refreshPlannerStatus);
	connect(m_previewRequest, &QPushButton::clicked, this, &LevelGenerationDialog::showRequestPreview);
	connect(m_details, &QToolButton::toggled, this, [this](bool shown) {
		m_detailTabs->setVisible(shown);
		m_details->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
	});
	connect(m_open, &QPushButton::clicked, this, &LevelGenerationDialog::openInLevels);
	connect(m_save, &QPushButton::clicked, this, &LevelGenerationDialog::saveAs);
	connect(m_savePlan, &QPushButton::clicked, this, &LevelGenerationDialog::savePlan);
	connect(m_saveReport, &QPushButton::clicked, this, &LevelGenerationDialog::saveReport);
	connect(m_generator, &QComboBox::currentIndexChanged, this, [this] {
		updateGeneratorControls();
		refreshPlannerStatus();
	});
	connect(m_game, &QComboBox::currentIndexChanged, this, &LevelGenerationDialog::updateGeneratorControls);
	auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	connect(shortcut, &QShortcut::activated, this, &LevelGenerationDialog::generate);
	updateGeneratorControls();
	refreshPlannerStatus();
}

LevelGenerationDialog::~LevelGenerationDialog()
{
	m_work->discard = true;
	m_work->cancelled->store(true);
	if (QThread* thread = m_work->thread) {
		thread->disconnect(this);
		thread->wait();
		delete thread;
		m_work->thread = nullptr;
	}
}

void LevelGenerationDialog::setPrompt(const QString& prompt)
{
	m_prompt->setPlainText(prompt);
}

bool LevelGenerationDialog::busy() const
{
	return m_busy;
}

const LevelGenerationResult& LevelGenerationDialog::result() const
{
	return m_result;
}

void LevelGenerationDialog::refreshPlannerStatus()
{
	const bool model = m_modelPlanner->isChecked();
	// The radio names the model it would ask, so the choice says who plans.
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	m_modelPlanner->setText(connection.ready() ? tr("The &text model: %1").arg(aiTextConnectionLabel(connection)) : tr("The &text model"));
	m_aiSettings->setVisible(model && m_hooks.openAiSettings);
	if (model) {
		m_plannerStatus->setText(aiTextConnectionBlockText(connection));
	} else if (mapgenSelected()) {
		m_plannerStatus->setText(tr("Quake-MapGen's rules plan the level here, the same way every time for the same description and seed."));
	} else {
		m_plannerStatus->setText(tr("The rules plan the level here, the same way every time for the same description and seed."));
	}
	m_previewRequest->setEnabled(model && !m_busy);
}

bool LevelGenerationDialog::mapgenSelected() const
{
	return choice(m_generator) == levelMapgenGeneratorId();
}

void LevelGenerationDialog::updateGeneratorControls()
{
	const bool mapgen = mapgenSelected();
	// Quake-MapGen builds no Doom levels; the studio generator has no capture the flag.
	setChoiceEnabled(m_game, QStringLiteral("doom"), !mapgen, tr("Quake-MapGen builds Quake, Quake II, and Quake III Arena levels."));
	if (mapgen && choice(m_game) == QStringLiteral("doom")) {
		m_game->setCurrentIndex(0);
	}
	setChoiceEnabled(m_mode, QStringLiteral("ctf"), mapgen, tr("Capture the flag needs Quake-MapGen."));
	if (!mapgen && choice(m_mode) == QStringLiteral("ctf")) {
		m_mode->setCurrentIndex(0);
	}
	const QString game = choice(m_game);
	const QString format = choice(m_format);
	m_format->blockSignals(true);
	m_format->clear();
	// With the game left to the description, the format follows whichever game it names.
	const QStringList formats = game.isEmpty() ? QStringList() : levelMapgenFormatIds(game);
	if (formats.isEmpty()) {
		m_format->addItem(tr("The game's own"), QString());
	}
	for (const QString& id : formats) {
		if (id == QStringLiteral("q3-legacy")) {
			m_format->addItem(tr("Classic brushes"), id);
		} else if (id == QStringLiteral("q3-brush-primitives")) {
			m_format->addItem(tr("Brush primitives"), id);
		} else {
			m_format->addItem(tr("Valve 220"), id);
		}
	}
	m_format->setCurrentIndex(std::max(0, m_format->findData(format)));
	m_format->setEnabled(m_format->count() > 1);
	m_format->blockSignals(false);
	// Each generator's own controls; Quake-MapGen reads the theme from the description.
	m_options->setRowVisible(m_format, mapgen);
	m_options->setRowVisible(m_intensity, mapgen);
	m_options->setRowVisible(m_theme, !mapgen);
	m_options->setRowVisible(m_liquid, !mapgen);
	m_options->setRowVisible(m_monsters, !mapgen);
	m_projectTextures->setVisible(!mapgen);
	m_rooms->setMaximum(mapgen ? 32 : 16);
}

LevelGenerationSpec LevelGenerationDialog::mapgenSpecFromControls() const
{
	LevelGenerationSpec spec;
	spec.generator = levelMapgenGeneratorId();
	spec.prompt = m_prompt->toPlainText().trimmed();
	spec.game = choice(m_game);
	spec.mode = choice(m_mode);
	spec.format = choice(m_format);
	spec.rooms = m_rooms->value();
	spec.verticality = choice(m_verticality);
	spec.intensity = choice(m_intensity);
	spec.players = m_players->value();
	spec.seed = m_seedFromPrompt->isChecked() ? -1 : m_seed->value();
	return spec;
}

void LevelGenerationDialog::generateWithMapgen()
{
	const LevelGenerationSpec spec = mapgenSpecFromControls();
	if (const QString problem = levelMapgenSpecProblem(spec); !problem.isEmpty()) {
		m_status->setText(problem);
		return;
	}
	const QString about = spec.prompt.isEmpty() ? m_game->currentText() : spec.prompt.left(80);
	if (m_rulesPlanner->isChecked()) {
		if (m_hooks.beginTask) {
			m_task = m_hooks.beginTask(tr("Generate Level"), tr("Quake-MapGen is planning \"%1\" by its rules.").arg(about));
		}
		runInBackground(tr("Planning, laying out, building, and analysing the level…"), [spec] { return generateMapgenLevel(spec); }, {});
		return;
	}
	if (spec.prompt.isEmpty()) {
		m_status->setText(tr("Describe the level for the text model to plan."));
		m_prompt->setFocus();
		return;
	}
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	refreshPlannerStatus();
	if (!connection.ready()) {
		m_status->setText(aiTextConnectionBlockText(connection));
		return;
	}
	if (!connection.local && m_hooks.confirmSend) {
		AiChatRequest request;
		AiHttpRequest http;
		QString error;
		if (!levelMapgenPlanAiRequest(spec, connection, &request, &error) || !buildAiHttpRequest(request, QString(), &http, &error)) {
			m_status->setText(error);
			return;
		}
		if (!m_hooks.confirmSend(connection.connectorId, connection.displayName, connection.endpoint, http)) {
			m_status->setText(tr("Nothing was sent."));
			return;
		}
	}
	if (m_hooks.beginTask) {
		m_task = m_hooks.beginTask(tr("Generate Level"), tr("Asking %1 to plan \"%2\" for Quake-MapGen.").arg(aiTextConnectionLabel(connection), about));
	}
	auto model = std::make_shared<LevelMapgenModel>();
	model->connection = connection;
	model->apiKey = aiTextConnectionApiKey(connection);
	model->cancelled = m_work->cancelled;
	runInBackground(tr("Asking %1 for a plan, then building and analysing the level…").arg(connection.displayName),
		[spec, model] { return generateMapgenLevel(spec, model.get()); }, {});
}

LevelGenerationSpec LevelGenerationDialog::specFromControls() const
{
	LevelGenerationSpec spec;
	spec.prompt = m_prompt->toPlainText().trimmed();
	spec.game = choice(m_game);
	spec.mode = choice(m_mode);
	spec.theme = choice(m_theme);
	spec.rooms = m_rooms->value();
	spec.verticality = choice(m_verticality);
	spec.liquid = choice(m_liquid);
	spec.monsters = choice(m_monsters);
	spec.players = m_players->value();
	spec.seed = m_seedFromPrompt->isChecked() ? -1 : m_seed->value();
	// The game the description chose, when Game leaves it to the description.
	spec = normalizedLevelGenerationSpec(spec);
	if (m_projectTextures->isChecked() && m_hooks.availableTextures) {
		spec.availableTextures = m_hooks.availableTextures(spec.game);
	}
	return spec;
}

void LevelGenerationDialog::generate()
{
	if (m_busy) {
		return;
	}
	if (mapgenSelected()) {
		generateWithMapgen();
		return;
	}
	const LevelGenerationSpec spec = specFromControls();
	if (m_seedFromPrompt->isChecked()) {
		m_seed->setValue(int(std::clamp<qint64>(spec.seed, 0, 999999999)));
	}
	if (m_rulesPlanner->isChecked()) {
		if (m_hooks.beginTask) {
			m_task = m_hooks.beginTask(tr("Generate Level"), tr("Planning \"%1\" by the rules.").arg(spec.title));
		}
		build(spec, nullptr, {});
		return;
	}
	if (spec.prompt.isEmpty()) {
		m_status->setText(tr("Describe the level for the text model to plan."));
		m_prompt->setFocus();
		return;
	}
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	refreshPlannerStatus();
	if (!connection.ready()) {
		m_status->setText(aiTextConnectionBlockText(connection));
		return;
	}
	if (!connection.local && m_hooks.confirmSend) {
		AiHttpRequest http;
		QString error;
		if (!buildAiHttpRequest(levelPlanAiRequest(spec, connection.connectorId, connection.model, connection.endpoint), QString(), &http, &error)) {
			m_status->setText(error);
			return;
		}
		if (!m_hooks.confirmSend(connection.connectorId, connection.displayName, connection.endpoint, http)) {
			m_status->setText(tr("Nothing was sent."));
			return;
		}
	}
	if (m_hooks.beginTask) {
		m_task = m_hooks.beginTask(tr("Generate Level"), tr("Asking %1 for a plan.").arg(aiTextConnectionLabel(connection)));
	}
	askModel(spec, connection, QString(), {}, 0);
}

void LevelGenerationDialog::askModel(const LevelGenerationSpec& spec, const AiTextConnection& connection, const QString& previousAnswer, const QStringList& problems, int attempt)
{
	setBusy(true, attempt == 0 ? tr("Asking %1 for a plan…").arg(connection.displayName)
							   : tr("Asking %1 to correct %n problem(s) in its plan…", nullptr, int(problems.size())).arg(connection.displayName));
	const AiChatRequest request = levelPlanAiRequest(spec, connection.connectorId, connection.model, connection.endpoint, previousAnswer, problems);
	QString error;
	const bool started = m_client->send(request, aiTextConnectionApiKey(connection), [this, spec, connection, attempt](const AiChatResponse& response) {
		if (!response.ok) {
			const bool cancelled = response.failure == AiChatFailure::Cancelled;
			const QString message = cancelled ? tr("Cancelled. Nothing was built.") : tr("The text model did not plan the level: %1").arg(response.errorMessage);
			setBusy(false, message);
			finishTask(false, cancelled, message);
			return;
		}
		LevelSemanticPlan plan;
		QStringList found;
		const bool read = levelSemanticPlanFromAnswer(response.text, spec, &plan, &found);
		if (!found.isEmpty() && attempt == 0) {
			askModel(spec, connection, response.text, found, 1);
			return;
		}
		QStringList warnings;
		if (!read) {
			warnings << tr("The text model's answer held no usable plan (%1); the rules planned the level instead.").arg(found.join(QLatin1Char(' ')));
			build(spec, nullptr, warnings);
			return;
		}
		for (const QString& problem : found) {
			warnings << tr("Repaired in the model's plan: %1").arg(problem);
		}
		plan.planner = QStringLiteral("ai:%1/%2").arg(connection.connectorId, connection.model);
		build(spec, &plan, warnings);
	}, &error);
	if (!started) {
		setBusy(false, error);
		finishTask(false, false, error);
	}
}

void LevelGenerationDialog::build(const LevelGenerationSpec& spec, const LevelSemanticPlan* plan, const QStringList& planWarnings)
{
	const bool hasPlan = plan != nullptr;
	const LevelSemanticPlan planCopy = hasPlan ? *plan : LevelSemanticPlan();
	runInBackground(tr("Laying out and building the level…"), [spec, planCopy, hasPlan] { return generateLevel(spec, hasPlan ? &planCopy : nullptr); }, planWarnings);
}

void LevelGenerationDialog::runInBackground(const QString& status, std::function<LevelGenerationResult()> job, const QStringList& planWarnings)
{
	setBusy(true, status);
	const auto work = m_work;
	work->discard = false;
	work->cancelled->store(false);
	QThread* thread = QThread::create([work, job = std::move(job)] { work->result = job(); });
	work->thread = thread;
	connect(thread, &QThread::finished, this, [this, work, thread, planWarnings] {
		thread->deleteLater();
		work->thread = nullptr;
		if (work->discard) {
			setBusy(false, tr("Cancelled. The level was not shown."));
			finishTask(false, true, tr("Cancelled."));
			return;
		}
		showResult(work->result, planWarnings);
	});
	thread->start();
}

void LevelGenerationDialog::showResult(const LevelGenerationResult& result, const QStringList& planWarnings)
{
	m_result = result;
	const bool mapgen = result.generator == levelMapgenGeneratorId();
	m_savePlan->setVisible(!mapgen);
	m_saveReport->setVisible(mapgen);
	m_detailTabs->setTabVisible(m_detailTabs->indexOf(m_analysisText), mapgen);
	m_detailTabs->setTabVisible(m_detailTabs->indexOf(m_reportText), mapgen);
	m_analysisText->setPlainText(result.analysis.join(QLatin1Char('\n')));
	m_reportText->setPlainText(QString::fromUtf8(result.generatorReport.left(1024 * 1024)));
	for (QPushButton* button : {m_open, m_save, m_savePlan}) {
		button->setEnabled(result.ok);
	}
	// A layout that failed validation still has its report to read.
	m_saveReport->setEnabled(!result.generatorReport.isEmpty());
	if (mapgen && m_seedFromPrompt->isChecked() && result.spec.seed >= 0) {
		m_seed->setValue(int(std::clamp<qint64>(result.spec.seed, 0, 999999999)));
	}
	if (!result.ok) {
		const QString message = tr("The level could not be built: %1").arg(result.error);
		setBusy(false, message);
		finishTask(false, m_work->cancelled->load(), message);
		return;
	}
	m_preview = renderLevelLayoutPreview(result, QSize(900, 900));
	updatePreview();
	const QStringList summary = levelGenerationSummaryLines(result);
	m_summary->setText(summary.join(QLatin1Char('\n')));
	m_previewImage->setAccessibleDescription(summary.join(QLatin1Char(' ')));
	m_planText->setPlainText(levelSemanticPlanLines(result.plan).join(QLatin1Char('\n')));
	QStringList notes = planWarnings;
	for (const QString& warning : result.warnings) {
		notes << tr("Warning: %1").arg(warning);
	}
	notes += result.notes;
	m_notesText->setPlainText(notes.isEmpty() ? tr("Nothing to note: every room was placed and linked.") : notes.join(QLatin1Char('\n')));
	m_mapText->setPlainText(result.format == QStringLiteral("doom-wad")
			? tr("A Doom PWAD of %n byte(s) holding %1; open it in Levels to edit its sectors.", nullptr, int(result.mapBytes.size())).arg(result.mapName)
			: QString::fromUtf8(result.mapBytes.left(512 * 1024)));
	m_notesText->setAccessibleDescription(notes.join(QLatin1Char(' ')));
	const QString done = (result.warnings.isEmpty() && planWarnings.isEmpty())
		? tr("Built \"%1\". Review it, then open it in Levels or save it.").arg(result.plan.title)
		: tr("Built \"%1\" with %n note(s) to review under Details.", nullptr, int(result.warnings.size() + planWarnings.size())).arg(result.plan.title);
	setBusy(false, done);
	finishTask(true, false, summary.value(0));
}

void LevelGenerationDialog::setBusy(bool busy, const QString& status)
{
	m_busy = busy;
	m_status->setText(status);
	m_progress->setVisible(busy);
	m_cancel->setEnabled(busy);
	m_generate->setEnabled(!busy);
	m_newSeed->setEnabled(!busy);
	m_previewRequest->setEnabled(!busy && m_modelPlanner->isChecked());
	for (QPushButton* button : {m_open, m_save, m_savePlan}) {
		button->setEnabled(!busy && m_result.ok);
	}
	m_saveReport->setEnabled(!busy && !m_result.generatorReport.isEmpty());
}

void LevelGenerationDialog::finishTask(bool succeeded, bool cancelled, const QString& summary)
{
	if (!m_task.isEmpty() && m_hooks.endTask) {
		m_hooks.endTask(m_task, succeeded, cancelled, summary);
	}
	m_task.clear();
}

void LevelGenerationDialog::cancelGeneration()
{
	if (!m_busy) {
		return;
	}
	if (m_client->busy()) {
		m_client->cancel();
		return;
	}
	// The build itself is quick; its result is dropped. A Quake-MapGen text
	// model stops waiting for its answer.
	m_work->discard = true;
	m_work->cancelled->store(true);
	m_status->setText(tr("Cancelling…"));
}

void LevelGenerationDialog::updatePreview()
{
	if (m_preview.isNull()) {
		return;
	}
	const QSize room = m_previewImage->size() - QSize(8, 8);
	if (room.width() < 16 || room.height() < 16) {
		return;
	}
	m_previewImage->setPixmap(QPixmap::fromImage(m_preview.scaled(room, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void LevelGenerationDialog::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	updatePreview();
}

void LevelGenerationDialog::reject()
{
	if (m_busy) {
		cancelGeneration();
		return;
	}
	QDialog::reject();
}

void LevelGenerationDialog::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && m_busy) {
		cancelGeneration();
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

void LevelGenerationDialog::showRequestPreview()
{
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	AiHttpRequest http;
	QString error;
	bool built = false;
	if (mapgenSelected()) {
		AiChatRequest request;
		built = levelMapgenPlanAiRequest(mapgenSpecFromControls(), connection, &request, &error) && buildAiHttpRequest(request, QString(), &http, &error);
	} else {
		const LevelGenerationSpec spec = specFromControls();
		built = buildAiHttpRequest(levelPlanAiRequest(spec, connection.connectorId, connection.model, connection.endpoint), QString(), &http, &error);
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelGenerationRequestPreview"));
	dialog.setWindowTitle(tr("Plan Request"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(760, 560);
	auto* layout = new QVBoxLayout(&dialog);
	auto* explanation = new QLabel(built ? QStringLiteral("%1 %2").arg(aiTextRequestPreviewNote(connection, tr("Generate")), aiTextConnectionBlockText(connection))
		: connection.ready()			 ? error
										 : aiTextConnectionBlockText(connection));
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto* text = readOnlyText(QStringLiteral("levelGenerationRequestText"), tr("The plan request"));
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setPlainText(built ? describeAiHttpRequest(http) : QString());
	layout->addWidget(text, 1);
	auto* raw = new QCheckBox(tr("Show the raw JSON"));
	raw->setEnabled(built);
	connect(raw, &QCheckBox::toggled, text, [text, http](bool shown) { text->setPlainText(describeAiHttpRequest(http, shown ? AiRequestView::Raw : AiRequestView::Readable)); });
	layout->addWidget(raw);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

void LevelGenerationDialog::saveAs()
{
	if (!m_result.ok) {
		return;
	}
	const bool doom = m_result.format == QStringLiteral("doom-wad");
	const QString folder = m_hooks.saveFolder ? m_hooks.saveFolder() : QString();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Generated Level"), QDir(folder).filePath(m_result.suggestedFileName),
		doom ? tr("Doom WAD (*.wad)") : tr("Map source (*.map)"));
	if (path.isEmpty()) {
		return;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(m_result.mapBytes) != m_result.mapBytes.size() || !file.commit()) {
		m_status->setText(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	m_status->setText(tr("Saved %1.").arg(QDir::toNativeSeparators(path)));
}

void LevelGenerationDialog::savePlan()
{
	if (!m_result.ok) {
		return;
	}
	const QString folder = m_hooks.saveFolder ? m_hooks.saveFolder() : QString();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Level Plan"), QDir(folder).filePath(QFileInfo(m_result.suggestedFileName).completeBaseName() + QStringLiteral(".plan.json")),
		tr("Level plan (*.json)"));
	if (path.isEmpty()) {
		return;
	}
	QJsonObject plan = levelSemanticPlanJson(m_result.plan);
	plan.insert(QStringLiteral("planner"), m_result.plan.planner);
	QSaveFile file(path);
	const QByteArray bytes = QJsonDocument(plan).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		m_status->setText(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	m_status->setText(tr("Saved the plan to %1.").arg(QDir::toNativeSeparators(path)));
}

void LevelGenerationDialog::saveReport()
{
	if (m_result.generatorReport.isEmpty()) {
		return;
	}
	const QString folder = m_hooks.saveFolder ? m_hooks.saveFolder() : QString();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Quake-MapGen Report"),
		QDir(folder).filePath(QFileInfo(m_result.suggestedFileName).completeBaseName() + QStringLiteral(".qmg.json")), tr("Quake-MapGen report (*.json)"));
	if (path.isEmpty()) {
		return;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(m_result.generatorReport) != m_result.generatorReport.size() || !file.commit()) {
		m_status->setText(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	m_status->setText(tr("Saved the report to %1.").arg(QDir::toNativeSeparators(path)));
}

void LevelGenerationDialog::openInLevels()
{
	if (!m_result.ok || !m_hooks.openInLevels) {
		return;
	}
	QString error;
	if (!m_hooks.openInLevels(m_result, &error)) {
		if (!error.isEmpty()) {
			m_status->setText(error);
		}
		return;
	}
	m_status->setText(tr("Opened \"%1\" in Levels as a new map. Save As gives it a file.").arg(m_result.plan.title));
}

} // namespace vibestudio
