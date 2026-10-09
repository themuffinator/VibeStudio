#include "app/release_dialog.h"

#include "app/studio_charts.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/ui_primitives.h"

#include "core/deflate.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <limits>

namespace vibestudio {

struct ReleaseDialog::Work {
	std::atomic_bool cancel {false};
	std::atomic_bool discarded {false};
	QMutex mutex;
	QString phase;
	qint64 done = 0;
	qint64 total = 0;
	QThread* thread = nullptr;
	bool planPending = false;
};

struct ReleaseDialog::PlanOutcome {
	quint64 generation = 0;
	bool withCatalog = false;
	bool withStock = false;
	ReleaseCatalog catalog;
	ReleaseStockContext stock;
	ReleasePlan plan;
	ProjectChangelog changelog;
	QVector<ChangelogEntry> changes;
	QVector<ChangelogEntry> additions;
	ReleaseInventoryDiff diff;
	QString notes;
	QString readme;
	QString error;
};

namespace {

QString humanSize(quint64 bytes)
{
	return QLocale().formattedDataSize(qint64(std::min<quint64>(bytes, quint64(std::numeric_limits<qint64>::max()))));
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QPlainTextEdit* editor(const QString& objectName, const QString& accessibleName, bool readOnly)
{
	auto* text = new QPlainTextEdit;
	text->setObjectName(objectName);
	text->setAccessibleName(accessibleName);
	text->setReadOnly(readOnly);
	text->setTabChangesFocus(true);
	text->setFont(studioMonospaceFont());
	return text;
}

QTreeWidget* tree(const QString& objectName, const QString& accessibleName, const QStringList& headers)
{
	auto* view = new QTreeWidget;
	view->setObjectName(objectName);
	view->setAccessibleName(accessibleName);
	view->setHeaderLabels(headers);
	view->setRootIsDecorated(false);
	view->setUniformRowHeights(true);
	view->setAlternatingRowColors(true);
	view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	view->header()->setStretchLastSection(true);
	view->header()->setSectionResizeMode(QHeaderView::Interactive);
	return view;
}

// Every column but the stretched last one fits its contents.
void fitColumns(QTreeWidget* view)
{
	for (int column = 0; column + 1 < view->columnCount(); ++column) {
		view->resizeColumnToContents(column);
	}
}

// Paths in a mixed-direction label read left to right.
QLineEdit* pathField(const QString& objectName, const QString& accessibleName)
{
	auto* field = new QLineEdit;
	field->setObjectName(objectName);
	field->setAccessibleName(accessibleName);
	field->setLayoutDirection(Qt::LeftToRight);
	return field;
}

void ownWorker(QThread* thread, const std::shared_ptr<void>& keepAlive)
{
	QObject::connect(thread, &QThread::finished, thread, [keepAlive]() {});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread]() { thread->wait(); });
}

} // namespace

ReleaseDialog::ReleaseDialog(QWidget* parent, ReleaseDialogHooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
	, m_work(std::make_shared<Work>())
{
	setObjectName(QStringLiteral("releaseDialog"));
	setWindowTitle(tr("Package and Release"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Choose what to release, review what ships and what the game already provides, edit the notes, then publish."));
	// Room for the review at any text size, within the screen.
	const QSize available = screen() ? screen()->availableGeometry().size() : QSize(1280, 900);
	const int line = fontMetrics().height();
	resize(std::min(std::max(1180, line * 74), std::max(640, available.width() - 40)), std::min(std::max(780, line * 49), std::max(480, available.height() - 40)));
	buildUi();
	QTimer::singleShot(0, this, [this]() {
		if (!m_loaded) {
			reload();
		}
	});
}

ReleaseDialog::~ReleaseDialog()
{
	m_work->discarded = true;
	m_work->cancel = true;
}

void ReleaseDialog::buildUi()
{
	auto* root = new QVBoxLayout(this);
	root->setSpacing(10);

	m_context = new QLabel;
	m_context->setObjectName(QStringLiteral("releaseContext"));
	m_context->setAccessibleName(tr("Release context"));
	m_context->setTextFormat(Qt::PlainText);
	m_context->setWordWrap(true);
	root->addWidget(m_context);
	m_notice = new NoticeBar;
	m_notice->setObjectName(QStringLiteral("releaseNotice"));
	m_notice->hide();
	root->addWidget(m_notice);

	auto* splitter = new QSplitter(Qt::Horizontal);
	splitter->setObjectName(QStringLiteral("releaseSplitter"));
	splitter->setChildrenCollapsible(false);
	root->addWidget(splitter, 1);

	// Left: what is released, and how it is described.
	auto* left = new QWidget;
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 8, 0);
	leftLayout->setSpacing(12);

	auto* what = new CardFrame(tr("What to Release"));
	m_scope = new QComboBox;
	m_scope->setObjectName(QStringLiteral("releaseScope"));
	m_scope->setAccessibleName(tr("What to release"));
	m_scope->addItem(studioIcon(QStringLiteral("project")), tr("Whole project"), QStringLiteral("project"));
	m_scope->addItem(studioIcon(QStringLiteral("map")), tr("Maps"), QStringLiteral("maps"));
	m_scope->addItem(studioIcon(QStringLiteral("model")), tr("Models"), QStringLiteral("models"));
	m_scope->addItem(studioIcon(QStringLiteral("texture")), tr("Textures"), QStringLiteral("textures"));
	what->bodyLayout()->addWidget(m_scope);
	m_scopeHint = new QLabel;
	m_scopeHint->setObjectName(QStringLiteral("releaseScopeHint"));
	m_scopeHint->setWordWrap(true);
	m_scopeHint->setTextFormat(Qt::PlainText);
	what->bodyLayout()->addWidget(m_scopeHint);
	m_items = new QListWidget;
	m_items->setObjectName(QStringLiteral("releaseItems"));
	m_items->setAccessibleName(tr("Items to release"));
	m_items->setAccessibleDescription(tr("Tick the maps, models or texture folders to release. Space toggles the current row."));
	m_items->setMinimumHeight(120);
	what->bodyLayout()->addWidget(m_items, 1);
	auto* itemActions = new QHBoxLayout;
	m_addItems = createButton(tr("Add Files…"), QStringLiteral("add"), QStringLiteral("ghost"));
	m_addItems->setObjectName(QStringLiteral("releaseAddItems"));
	m_addItems->setAccessibleName(tr("Add files to release"));
	m_addItems->setToolTip(tr("Choose maps, models or textures that the project list does not show."));
	itemActions->addWidget(m_addItems);
	itemActions->addStretch(1);
	what->bodyLayout()->addLayout(itemActions);
	m_useOpenPackage = new QCheckBox;
	m_useOpenPackage->setObjectName(QStringLiteral("releaseUseOpenPackage"));
	m_useOpenPackage->setToolTip(tr("Also take files from the package open on the Packages page, layered over the project. Leave this off unless that package holds your own work."));
	what->bodyLayout()->addWidget(m_useOpenPackage);
	leftLayout->addWidget(what, 1);

	auto* details = new CardFrame(tr("Release"));
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_title = new QLineEdit;
	m_title->setObjectName(QStringLiteral("releaseTitle"));
	m_title->setAccessibleName(tr("Release title"));
	form->addRow(tr("&Title:"), m_title);
	auto* versionRow = new QHBoxLayout;
	m_version = new QLineEdit;
	m_version->setObjectName(QStringLiteral("releaseVersion"));
	m_version->setAccessibleName(tr("Version"));
	m_version->setLayoutDirection(Qt::LeftToRight);
	m_version->setToolTip(tr("Letters, digits, '.', '-', '+' or '_', such as 1.2.0. Semantic Versioning suits most releases."));
	m_nextVersion = new QToolButton;
	m_nextVersion->setObjectName(QStringLiteral("releaseNextVersion"));
	m_nextVersion->setText(tr("Next"));
	m_nextVersion->setAccessibleName(tr("Choose the next version"));
	m_nextVersion->setPopupMode(QToolButton::InstantPopup);
	m_nextVersion->setMenu(new QMenu(m_nextVersion));
	versionRow->addWidget(m_version, 1);
	versionRow->addWidget(m_nextVersion);
	// A row holding a layout gets no buddy of its own, so its mnemonic would
	// show as a literal '&'.
	auto* versionLabel = new QLabel(tr("&Version:"));
	versionLabel->setBuddy(m_version);
	form->addRow(versionLabel, versionRow);
	m_versionState = new QLabel;
	m_versionState->setObjectName(QStringLiteral("releaseVersionState"));
	m_versionState->setWordWrap(true);
	m_versionState->setTextFormat(Qt::PlainText);
	m_versionState->hide();
	form->addRow(QString(), m_versionState);
	m_authors = new QLineEdit;
	m_authors->setObjectName(QStringLiteral("releaseAuthors"));
	m_authors->setAccessibleName(tr("Authors"));
	m_authors->setPlaceholderText(tr("Name <email>, separated by commas"));
	form->addRow(tr("&Authors:"), m_authors);
	m_description = new QPlainTextEdit;
	m_description->setObjectName(QStringLiteral("releaseDescription"));
	m_description->setAccessibleName(tr("Description"));
	m_description->setTabChangesFocus(true);
	m_description->setMaximumHeight(fontMetrics().lineSpacing() * 4 + 16);
	form->addRow(tr("&Description:"), m_description);
	m_website = new QLineEdit;
	m_website->setObjectName(QStringLiteral("releaseWebsite"));
	m_website->setAccessibleName(tr("Website"));
	m_website->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("&Website:"), m_website);
	m_license = new QLineEdit;
	m_license->setObjectName(QStringLiteral("releaseLicense"));
	m_license->setAccessibleName(tr("Licence and permissions"));
	m_license->setPlaceholderText(tr("For example: free to distribute unmodified"));
	form->addRow(tr("&Licence:"), m_license);
	details->bodyLayout()->addLayout(form);
	leftLayout->addWidget(details);

	auto* package = new CardFrame(tr("Package"));
	auto* packageForm = new QFormLayout;
	packageForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	packageForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_format = new QComboBox;
	m_format->setObjectName(QStringLiteral("releaseFormat"));
	m_format->setAccessibleName(tr("Package format"));
	packageForm->addRow(tr("&Format:"), m_format);
	m_packageName = new QLineEdit;
	m_packageName->setObjectName(QStringLiteral("releasePackageName"));
	m_packageName->setAccessibleName(tr("Package file name"));
	m_packageName->setLayoutDirection(Qt::LeftToRight);
	packageForm->addRow(tr("File &name:"), m_packageName);
	m_gameFolder = new QLineEdit;
	m_gameFolder->setObjectName(QStringLiteral("releaseGameFolder"));
	m_gameFolder->setAccessibleName(tr("Game folder players install into"));
	m_gameFolder->setLayoutDirection(Qt::LeftToRight);
	m_gameFolder->setToolTip(tr("The base game's folder for maps and add-ons, or a folder of its own for a mod."));
	packageForm->addRow(tr("&Game folder:"), m_gameFolder);
	m_compression = new QComboBox;
	m_compression->setObjectName(QStringLiteral("releaseCompression"));
	m_compression->setAccessibleName(tr("Compression"));
	m_compression->addItem(tr("Default"), QStringLiteral("default"));
	m_compression->addItem(tr("Best"), QStringLiteral("best"));
	m_compression->addItem(tr("Fast"), QStringLiteral("fast"));
	m_compression->addItem(tr("Store (no compression)"), QStringLiteral("store"));
	packageForm->addRow(tr("&Compression:"), m_compression);
	m_includeSources = new QCheckBox(tr("Include &sources (maps and source art)"));
	m_includeSources->setObjectName(QStringLiteral("releaseIncludeSources"));
	m_includeSources->setToolTip(tr("Ship the .map files and source art too, for players who want to learn from or remix the release."));
	packageForm->addRow(m_includeSources);
	package->bodyLayout()->addLayout(packageForm);
	leftLayout->addWidget(package);

	auto* leftScroll = new QScrollArea;
	leftScroll->setObjectName(QStringLiteral("releaseOptionsScroll"));
	leftScroll->setAccessibleName(tr("Release options"));
	leftScroll->setWidgetResizable(true);
	leftScroll->setFrameShape(QFrame::NoFrame);
	leftScroll->setWidget(left);
	splitter->addWidget(leftScroll);

	// Right: the review.
	auto* right = new QWidget;
	auto* rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(8, 0, 0, 0);
	rightLayout->setSpacing(10);
	m_state = new LoadingPane;
	m_state->setObjectName(QStringLiteral("releaseState"));
	m_state->setAccessibleName(tr("Release review state"));
	m_state->setTitle(tr("Review"));
	m_state->setReducedMotion(StudioSettings().accessibilityPreferences().reducedMotion);
	rightLayout->addWidget(m_state);
	auto* chips = new QHBoxLayout;
	chips->setSpacing(6);
	m_includedChip = createStatusChip(tr("Files in the release"), QStringLiteral("package"));
	m_includedChip->setObjectName(QStringLiteral("releaseIncludedChip"));
	m_stockChip = createStatusChip(tr("Provided by the game"), QStringLiteral("game"));
	m_stockChip->setObjectName(QStringLiteral("releaseStockChip"));
	m_overrideChip = createStatusChip(tr("Files that replace the game's"), QStringLiteral("warning"));
	m_overrideChip->setObjectName(QStringLiteral("releaseOverrideChip"));
	m_problemChip = createStatusChip(tr("Problems"), QStringLiteral("error"));
	m_problemChip->setObjectName(QStringLiteral("releaseProblemChip"));
	for (QToolButton* chip : {m_includedChip, m_stockChip, m_overrideChip, m_problemChip}) {
		chips->addWidget(chip);
	}
	chips->addStretch(1);
	rightLayout->addLayout(chips);
	m_chart = new CompositionChart;
	m_chart->setObjectName(QStringLiteral("releaseComposition"));
	m_chart->setAccessibleName(tr("Release contents by kind"));
	m_chart->setTitle(tr("Contents by kind"));
	m_chart->setEmptyText(tr("Nothing planned yet."));
	rightLayout->addWidget(m_chart);

	m_tabs = createPanelTabs(tr("Release review"));
	m_tabs->setObjectName(QStringLiteral("releaseTabs"));
	m_included = tree(QStringLiteral("releaseIncluded"), tr("Files in the release"), {tr("Path"), tr("Kind"), tr("Size"), tr("Needed by")});
	m_tabs->addTab(m_included, studioIcon(QStringLiteral("package")), tr("Included"));
	m_provided = tree(QStringLiteral("releaseProvided"), tr("Provided by the game"), {tr("Reference"), tr("Provided by"), tr("Path"), tr("Needed by")});
	m_tabs->addTab(m_provided, studioIcon(QStringLiteral("game")), tr("From the Game"));
	m_problems = tree(QStringLiteral("releaseProblems"), tr("Release problems"), {tr("Problem"), tr("Reference"), tr("Needed by")});
	m_problems->setAccessibleDescription(tr("Blocking problems come first. Activate an unbuilt map to open it on the Build page."));
	m_problems->setWordWrap(true);
	m_tabs->addTab(m_problems, studioIcon(QStringLiteral("warning")), tr("Problems"));

	auto* notesPage = new QWidget;
	auto* notesLayout = new QVBoxLayout(notesPage);
	notesLayout->setContentsMargins(0, 8, 0, 0);
	auto* changes = new QLabel(tr("Unreleased changes in the changelog:"));
	notesLayout->addWidget(changes);
	m_unreleased = new QListWidget;
	m_unreleased->setObjectName(QStringLiteral("releaseUnreleased"));
	m_unreleased->setAccessibleName(tr("Unreleased changes"));
	m_unreleased->setMaximumHeight(fontMetrics().lineSpacing() * 6 + 12);
	notesLayout->addWidget(m_unreleased);
	auto* addRow = new QHBoxLayout;
	m_changeCategory = new QComboBox;
	m_changeCategory->setObjectName(QStringLiteral("releaseChangeCategory"));
	m_changeCategory->setAccessibleName(tr("Change category"));
	for (const QString& category : changelogCategories()) {
		m_changeCategory->addItem(changelogCategoryDisplayName(category), category);
	}
	m_changeText = new QLineEdit;
	m_changeText->setObjectName(QStringLiteral("releaseChangeText"));
	m_changeText->setAccessibleName(tr("Change to record"));
	m_changeText->setPlaceholderText(tr("Describe a change, such as \"New arena: The Pit\""));
	m_addChange = createButton(tr("Record Change"), QStringLiteral("add"));
	m_addChange->setObjectName(QStringLiteral("releaseAddChange"));
	m_addChange->setAccessibleName(tr("Record the change in the changelog"));
	m_addChange->setToolTip(tr("Adds the change to the Unreleased section of the project's changelog now."));
	addRow->addWidget(m_changeCategory);
	addRow->addWidget(m_changeText, 1);
	addRow->addWidget(m_addChange);
	notesLayout->addLayout(addRow);
	auto* notesHeader = new QHBoxLayout;
	notesHeader->addWidget(new QLabel(tr("Release notes (Markdown):")), 1);
	m_regenerate = createButton(tr("Regenerate"), QStringLiteral("refresh"), QStringLiteral("ghost"));
	m_regenerate->setObjectName(QStringLiteral("releaseRegenerate"));
	m_regenerate->setAccessibleName(tr("Regenerate the notes and readme"));
	m_regenerate->setToolTip(tr("Rebuild the notes and readme from the plan and changelog, discarding your edits to them."));
	notesHeader->addWidget(m_regenerate);
	notesLayout->addLayout(notesHeader);
	m_notes = editor(QStringLiteral("releaseNotes"), tr("Release notes"), false);
	const QString tokens = tr("%1 and %2 are filled in when the package is written.").arg(releasePackageHashToken(), releasePackageSizeToken());
	m_notes->setToolTip(tokens);
	m_notes->setAccessibleDescription(tokens);
	notesLayout->addWidget(m_notes, 1);
	m_tabs->addTab(notesPage, studioIcon(QStringLiteral("book")), tr("Notes"));
	m_readme = editor(QStringLiteral("releaseReadme"), tr("Readme for players"), false);
	m_readme->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_readme->setToolTip(tokens);
	m_readme->setAccessibleDescription(tokens);
	m_tabs->addTab(m_readme, studioIcon(QStringLiteral("document")), tr("Readme"));
	m_details = editor(QStringLiteral("releaseDetails"), tr("Plan details"), true);
	m_details->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_tabs->addTab(m_details, studioIcon(QStringLiteral("details")), tr("Details"));
	rightLayout->addWidget(m_tabs, 1);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	// In text lines, so the options keep their room at larger text sizes.
	splitter->setSizes({fontMetrics().height() * 25, fontMetrics().height() * 49});

	// Bottom: where it goes, and the publish action.
	auto* output = new QHBoxLayout;
	auto* outputLabel = new QLabel(tr("&Output folder:"));
	m_output = pathField(QStringLiteral("releaseOutput"), tr("Release output folder"));
	// Empty only without a project, which has no default release folder.
	m_output->setPlaceholderText(tr("Choose a folder for the release files"));
	outputLabel->setBuddy(m_output);
	auto* chooseOutput = createButton(tr("Choose…"), QStringLiteral("folder-open"), QStringLiteral("ghost"));
	chooseOutput->setAccessibleName(tr("Choose the release output folder"));
	output->addWidget(outputLabel);
	output->addWidget(m_output, 1);
	output->addWidget(chooseOutput);
	root->addLayout(output);
	auto* options = new QHBoxLayout;
	m_archive = new QCheckBox(tr("Write a &distribution archive (.zip)"));
	m_archive->setObjectName(QStringLiteral("releaseArchive"));
	m_archive->setChecked(true);
	m_archive->setToolTip(tr("One ZIP with the package, the readme and any loose files, in the layout players extract."));
	m_updateChangelog = new QCheckBox(tr("Move &unreleased changes under this version"));
	m_updateChangelog->setObjectName(QStringLiteral("releaseUpdateChangelog"));
	m_updateChangelog->setChecked(true);
	m_replace = new QCheckBox(tr("&Replace an earlier release of this version"));
	m_replace->setObjectName(QStringLiteral("releaseReplace"));
	m_replace->setToolTip(tr("Rewrite the release files, keeping .bak copies of the old ones."));
	options->addWidget(m_archive);
	options->addWidget(m_updateChangelog);
	options->addWidget(m_replace);
	options->addStretch(1);
	root->addLayout(options);
	auto* footer = new QHBoxLayout;
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("releaseStatus"));
	m_status->setAccessibleName(tr("Release status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_progress = new QProgressBar;
	m_progress->setObjectName(QStringLiteral("releaseProgress"));
	m_progress->setAccessibleName(tr("Release progress"));
	m_progress->setVisible(false);
	m_progress->setMaximumWidth(220);
	m_reveal = createButton(tr("Show Folder"), QStringLiteral("reveal"), QStringLiteral("ghost"));
	m_reveal->setObjectName(QStringLiteral("releaseReveal"));
	m_reveal->setAccessibleName(tr("Show the release folder"));
	m_reveal->setVisible(false);
	m_cancel = createButton(tr("Cancel"), QStringLiteral("stop"), QStringLiteral("ghost"));
	m_cancel->setObjectName(QStringLiteral("releaseCancel"));
	m_cancel->setAccessibleName(tr("Cancel the current step"));
	m_cancel->setEnabled(false);
	m_publish = createButton(tr("&Publish Release"), QStringLiteral("rocket"), QStringLiteral("primary"));
	m_publish->setObjectName(QStringLiteral("releasePublish"));
	m_publish->setAccessibleName(tr("Publish the release"));
	m_publish->setToolTip(tr("Write the package, readme, notes and archive, record the release and update the changelog (Ctrl+Enter)."));
	m_publish->setEnabled(false);
	auto* close = createButton(tr("Close"), QString(), QStringLiteral("ghost"));
	close->setObjectName(QStringLiteral("releaseClose"));
	footer->addWidget(m_status, 1);
	footer->addWidget(m_progress);
	footer->addWidget(m_reveal);
	footer->addWidget(m_cancel);
	footer->addWidget(m_publish);
	footer->addWidget(close);
	root->addLayout(footer);

	m_planTimer = new QTimer(this);
	m_planTimer->setSingleShot(true);
	m_planTimer->setInterval(350);
	connect(m_planTimer, &QTimer::timeout, this, [this]() { startPlan(); });
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(120);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		QString phase;
		qint64 done = 0;
		qint64 total = 0;
		{
			QMutexLocker locker(&m_work->mutex);
			phase = m_work->phase;
			done = m_work->done;
			total = m_work->total;
		}
		if (!phase.isEmpty()) {
			m_state->setDetail(phase);
		}
		if (total > 0) {
			// Byte counts can exceed an int; show the proportion.
			m_progress->setRange(0, 1000);
			m_progress->setValue(int(std::clamp<qint64>(done * 1000 / std::max<qint64>(total, 1), 0, 1000)));
		} else {
			m_progress->setRange(0, 0);
		}
	});

	const auto replan = [this]() {
		if (!m_settingText) {
			schedulePlan();
		}
	};
	connect(m_scope, &QComboBox::currentIndexChanged, this, [this, replan]() {
		populateItems();
		replan();
	});
	connect(m_items, &QListWidget::itemChanged, this, replan);
	connect(m_useOpenPackage, &QCheckBox::toggled, this, replan);
	connect(m_format, &QComboBox::currentIndexChanged, this, replan);
	connect(m_includeSources, &QCheckBox::toggled, this, replan);
	for (QLineEdit* field : {m_title, m_packageName, m_gameFolder, m_authors, m_website, m_license}) {
		connect(field, &QLineEdit::textEdited, this, replan);
	}
	connect(m_description, &QPlainTextEdit::textChanged, this, replan);
	connect(m_version, &QLineEdit::textEdited, this, [this, replan]() {
		refreshVersionState();
		replan();
	});
	connect(m_notes, &QPlainTextEdit::textChanged, this, [this]() {
		if (!m_settingText) {
			m_notesEdited = true;
		}
	});
	connect(m_readme, &QPlainTextEdit::textChanged, this, [this]() {
		if (!m_settingText) {
			m_readmeEdited = true;
		}
	});
	connect(m_regenerate, &QPushButton::clicked, this, [this]() {
		m_notesEdited = false;
		m_readmeEdited = false;
		regenerateNotes();
	});
	connect(m_addChange, &QPushButton::clicked, this, &ReleaseDialog::addChangelogEntry);
	connect(m_changeText, &QLineEdit::returnPressed, this, &ReleaseDialog::addChangelogEntry);
	connect(m_addItems, &QPushButton::clicked, this, &ReleaseDialog::addItems);
	connect(chooseOutput, &QPushButton::clicked, this, &ReleaseDialog::chooseOutput);
	connect(m_output, &QLineEdit::textEdited, this, [this]() { refreshPublishState(); });
	connect(m_replace, &QCheckBox::toggled, this, [this]() {
		refreshVersionState();
		refreshPublishState();
	});
	connect(m_problems, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { activateProblem(item); });
	connect(m_publish, &QPushButton::clicked, this, &ReleaseDialog::publish);
	connect(m_cancel, &QPushButton::clicked, this, [this]() {
		m_work->cancel = true;
		m_cancel->setEnabled(false);
		m_status->setText(tr("Cancelling…"));
	});
	connect(m_reveal, &QPushButton::clicked, this, [this]() {
		if (m_hooks.reveal && !m_result.outputDirectory.isEmpty()) {
			m_hooks.reveal(m_result.outputDirectory);
		}
	});
	connect(close, &QPushButton::clicked, this, &ReleaseDialog::reject);
	connect(m_includedChip, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_included); });
	connect(m_stockChip, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_provided); });
	connect(m_overrideChip, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_included); });
	connect(m_problemChip, &QToolButton::clicked, this, [this]() { m_tabs->setCurrentWidget(m_problems); });
	setStatusChip(m_includedChip, operationStateId(OperationState::Idle), tr("No files yet"));
	setStatusChip(m_stockChip, operationStateId(OperationState::Idle), tr("Game files: not checked"));
	setStatusChip(m_overrideChip, operationStateId(OperationState::Idle), tr("Replaces: none"));
	setStatusChip(m_problemChip, operationStateId(OperationState::Idle), tr("Problems: none"));
}

void ReleaseDialog::setSelection(ReleaseScope scope, const QStringList& items)
{
	m_pendingScope = scope;
	m_pendingItems.clear();
	for (const QString& item : items) {
		m_pendingItems << QDir::cleanPath(QFileInfo(item).absoluteFilePath());
	}
	if (!m_pendingItems.isEmpty()) {
		// A named selection replaces earlier ticks, in every scope.
		m_ticked.clear();
		const QSignalBlocker blocker(m_items);
		for (int i = 0; i < m_items->count(); ++i) {
			if (m_items->item(i)->flags() & Qt::ItemIsUserCheckable) {
				m_items->item(i)->setCheckState(Qt::Unchecked);
			}
		}
	}
	{
		const QSignalBlocker blocker(m_scope);
		m_scope->setCurrentIndex(std::max(0, m_scope->findData(releaseScopeId(scope))));
	}
	if (m_loaded) {
		populateItems();
		schedulePlan();
	}
}

void ReleaseDialog::reload()
{
	m_loaded = true;
	m_manifest = m_hooks.project ? m_hooks.project() : ProjectManifest {};
	m_hasInstallation = m_hooks.installation && m_hooks.installation(&m_installation);
	m_gameKey = effectiveProjectGameKey(m_manifest, m_hasInstallation ? &m_installation : nullptr);
	loadProjectFields();
	const QString packageLabel = m_hooks.openPackageLabel ? m_hooks.openPackageLabel() : QString();
	const bool havePackage = m_hooks.openPackage && m_hooks.openPackage() && !packageLabel.isEmpty();
	m_useOpenPackage->setText(havePackage ? tr("Also use the open package (%1)").arg(packageLabel) : tr("Also use the open package"));
	m_useOpenPackage->setAccessibleName(m_useOpenPackage->text());
	m_useOpenPackage->setEnabled(havePackage);
	if (!havePackage) {
		const QSignalBlocker blocker(m_useOpenPackage);
		m_useOpenPackage->setChecked(false);
	}
	const QString project = m_manifest.rootPath.isEmpty() ? tr("No project open") : m_manifest.displayName;
	m_context->setText(tr("%1  ·  %2  ·  %3").arg(project, gameDefinitionForKey(m_gameKey).displayName,
		m_hasInstallation ? m_installation.displayName : tr("no installation linked")));
	m_context->setToolTip(nativePath(m_manifest.rootPath));
	startPlan();
}

void ReleaseDialog::loadProjectFields()
{
	const ProjectReleaseSettings release = effectiveProjectReleaseSettings(m_manifest, m_gameKey);
	m_settingText = true;
	m_title->setText(release.title);
	QString version = release.version;
	// Offer the next version when this one has already shipped.
	if (!m_manifest.rootPath.isEmpty() && QFileInfo::exists(releaseRecordPath(m_manifest.rootPath, version))) {
		version = bumpReleaseVersion(version, QStringLiteral("patch"));
	}
	m_version->setText(version);
	m_authors->setText(release.authors.join(QStringLiteral(", ")));
	m_description->setPlainText(release.description);
	m_website->setText(release.website);
	m_license->setText(release.license);
	m_packageName->setText(release.packageName);
	m_gameFolder->setText(release.gameFolder);
	m_gameFolder->setPlaceholderText(gameDefinitionForKey(m_gameKey).baseGameDirectory);
	m_includeSources->setChecked(release.includeSources);
	// Formats the game can load, its usual one first.
	m_format->clear();
	m_format->addItem(tr("Automatic"), QString());
	const bool doom = m_gameKey == QStringLiteral("doom") || m_gameKey == QStringLiteral("heretic-hexen");
	if (m_gameKey == QStringLiteral("quake3")) {
		m_format->addItem(tr("PK3 package"), QStringLiteral("pk3"));
		m_format->addItem(tr("ZIP of loose files"), QStringLiteral("zip"));
	} else if (m_gameKey == QStringLiteral("quake") || m_gameKey == QStringLiteral("quake2")) {
		m_format->addItem(tr("ZIP of loose files"), QStringLiteral("zip"));
		m_format->addItem(tr("PAK package"), QStringLiteral("pak"));
		m_format->addItem(tr("PK3 package (source ports)"), QStringLiteral("pk3"));
	} else if (doom) {
		m_format->addItem(tr("WAD"), QStringLiteral("wad"));
		m_format->addItem(tr("PK3 package (ZDoom family)"), QStringLiteral("pk3"));
		m_format->addItem(tr("ZIP of loose files"), QStringLiteral("zip"));
	} else {
		m_format->addItem(tr("ZIP of loose files"), QStringLiteral("zip"));
		m_format->addItem(tr("PK3 package"), QStringLiteral("pk3"));
	}
	const int format = m_format->findData(m_manifest.release.packageFormat);
	m_format->setCurrentIndex(m_manifest.release.packageFormat.isEmpty() || format < 0 ? 0 : format);
	m_settingText = false;
	refreshDefaultOutput();
	refreshVersionState();
}

ReleaseScope ReleaseDialog::currentScope() const
{
	return releaseScopeFromId(m_scope->currentData().toString());
}

void ReleaseDialog::populateItems()
{
	const ReleaseScope scope = currentScope();
	const QSignalBlocker blocker(m_items);
	// Ticks last across rebuilds and scopes: the list shows one scope at a time.
	for (int i = 0; i < m_items->count(); ++i) {
		const QListWidgetItem* item = m_items->item(i);
		if (item->flags() & Qt::ItemIsUserCheckable) {
			const QString path = item->data(Qt::UserRole).toString();
			if (item->checkState() == Qt::Checked) {
				m_ticked.insert(path);
			} else {
				m_ticked.remove(path);
			}
		}
	}
	for (const QString& item : std::as_const(m_pendingItems)) {
		m_ticked.insert(item);
	}
	struct Row {
		QString path;
		QString text;
		QIcon icon;
		QString tip;
		bool byDefault = false;
	};
	QVector<Row> rows;
	const QDir root(m_manifest.rootPath);
	const auto relative = [&](const QString& path) {
		const QString text = m_manifest.rootPath.isEmpty() ? path : root.relativeFilePath(path);
		return text.startsWith(QStringLiteral("..")) ? nativePath(path) : text;
	};
	const auto add = [&](const QString& path, const QString& text, const QIcon& icon, const QString& tip, bool byDefault) {
		rows << Row {path, text, icon, tip, byDefault};
	};
	QSet<QString> listed;
	switch (scope) {
	case ReleaseScope::Project:
		m_scopeHint->setText(tr("Everything in the project that the game does not already have: built maps, textures, shaders, models, sounds and scripts. Sources stay out unless you include them."));
		break;
	case ReleaseScope::Maps:
		m_scopeHint->setText(tr("Each map ships with its compiled BSP, the files the engine loads beside it, and every custom asset it uses."));
		for (const ReleaseMapInfo& map : std::as_const(m_catalog.maps)) {
			const QString path = map.sourcePath.isEmpty() ? map.compiledPath : map.sourcePath;
			const QString state = !map.built ? tr("not built") : map.stale ? tr("out of date") : tr("built");
			const QString names = map.doomMaps.isEmpty() ? map.name : QStringLiteral("%1 (%2)").arg(map.name, map.doomMaps.join(QStringLiteral(", ")));
			add(path, tr("%1  ·  %2").arg(names, state),
				studioIcon(QStringLiteral("map"), !map.built ? StudioIconTone::Danger : map.stale ? StudioIconTone::Warning : StudioIconTone::Success),
				nativePath(path), m_catalog.maps.size() == 1);
			listed.insert(path);
		}
		break;
	case ReleaseScope::Models:
		m_scopeHint->setText(tr("Each model ships with its skins, shader scripts and the images they use."));
		for (const QString& model : std::as_const(m_catalog.models)) {
			add(model, relative(model), studioIcon(QStringLiteral("model")), nativePath(model), false);
			listed.insert(model);
		}
		break;
	case ReleaseScope::Textures:
		m_scopeHint->setText(tr("Each folder ships its images and the shader scripts that declare shaders in it."));
		for (const QString& folder : std::as_const(m_catalog.textureFolders)) {
			add(folder, relative(folder), studioIcon(QStringLiteral("texture")), nativePath(folder), false);
			listed.insert(folder);
		}
		break;
	}
	if (scope != ReleaseScope::Project) {
		for (const QString& extra : std::as_const(m_extraItems) + m_pendingItems) {
			if (!listed.contains(extra)) {
				listed.insert(extra);
				add(extra, relative(extra), studioIcon(QStringLiteral("file")), nativePath(extra), true);
			}
		}
	}
	// Defaults apply only while nothing of this scope is ticked.
	const bool anyTicked = std::any_of(rows.cbegin(), rows.cend(), [this](const Row& row) { return m_ticked.contains(row.path); });
	m_items->clear();
	for (const Row& row : std::as_const(rows)) {
		auto* item = new QListWidgetItem(row.icon, row.text);
		item->setData(Qt::UserRole, row.path);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		item->setCheckState((anyTicked ? m_ticked.contains(row.path) : row.byDefault) ? Qt::Checked : Qt::Unchecked);
		item->setToolTip(row.tip);
		item->setData(Qt::AccessibleTextRole, row.text);
		m_items->addItem(item);
	}
	if (scope != ReleaseScope::Project) {
		if (m_items->count() == 0) {
			auto* empty = new QListWidgetItem(tr("Nothing of this kind in the project yet. Use Add Files to choose some."));
			empty->setFlags(Qt::NoItemFlags);
			m_items->addItem(empty);
		}
	}
	m_items->setVisible(scope != ReleaseScope::Project);
	m_addItems->setVisible(scope != ReleaseScope::Project);
	m_pendingItems.clear();
}

QStringList ReleaseDialog::checkedItems() const
{
	QStringList items;
	for (int i = 0; i < m_items->count(); ++i) {
		const QListWidgetItem* item = m_items->item(i);
		if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked) {
			items << item->data(Qt::UserRole).toString();
		}
	}
	return items;
}

ProjectReleaseSettings ReleaseDialog::releaseFromFields() const
{
	ProjectReleaseSettings release = effectiveProjectReleaseSettings(m_manifest, m_gameKey);
	release.title = m_title->text().trimmed().isEmpty() ? release.title : m_title->text().trimmed();
	release.version = m_version->text().trimmed();
	release.authors.clear();
	for (const QString& author : m_authors->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
		if (!author.trimmed().isEmpty()) {
			release.authors << author.trimmed();
		}
	}
	release.description = m_description->toPlainText().trimmed();
	release.website = m_website->text().trimmed();
	release.license = m_license->text().trimmed();
	const QString packageName = releaseSlug(m_packageName->text());
	release.packageName = packageName.isEmpty() ? releaseSlug(release.title) : packageName;
	if (release.packageName.isEmpty()) {
		release.packageName = QStringLiteral("release");
	}
	release.packageFormat = m_format->currentData().toString();
	release.gameFolder = m_gameFolder->text().trimmed().isEmpty() ? gameDefinitionForKey(m_gameKey).baseGameDirectory : m_gameFolder->text().trimmed();
	release.includeSources = m_includeSources->isChecked();
	return release;
}

void ReleaseDialog::schedulePlan()
{
	m_planTimer->start();
	m_state->setState(OperationState::Queued, tr("Waiting to review"));
	refreshDefaultOutput();
	refreshPublishState();
}

void ReleaseDialog::refreshDefaultOutput()
{
	// Follow the version and package name until the user picks a folder.
	const QString current = QDir::cleanPath(QDir::fromNativeSeparators(m_output->text().trimmed()));
	if (!m_defaultOutput.isEmpty() && current != m_defaultOutput) {
		return;
	}
	ProjectReleaseSettings release = releaseFromFields();
	const QString next = defaultReleaseOutputDirectory(m_manifest, release);
	if (!next.isEmpty()) {
		m_defaultOutput = QDir::cleanPath(next);
		m_output->setText(nativePath(next));
	}
}

void ReleaseDialog::startPlan()
{
	if (m_work->thread) {
		// Let the running review stop, then start again with the latest choices.
		m_work->planPending = true;
		m_work->cancel = true;
		return;
	}
	if (m_busy) {
		return;
	}
	auto outcome = std::make_shared<PlanOutcome>();
	outcome->generation = ++m_generation;
	outcome->withCatalog = !m_catalogLoaded;
	outcome->withStock = !m_stock.available && m_stock.warnings.isEmpty();
	const ProjectManifest manifest = m_manifest;
	const QString gameKey = m_gameKey;
	const ProjectReleaseSettings release = releaseFromFields();
	const ReleaseScope scope = currentScope();
	const QStringList items = scope == ReleaseScope::Project ? QStringList() : checkedItems() + (outcome->withCatalog ? m_pendingItems : QStringList());
	const GameInstallationProfile installation = m_installation;
	const bool hasInstallation = m_hasInstallation;
	const ReleaseStockContext stock = m_stock;
	QVector<LayeredPackageReader::Layer> layers;
	if (m_useOpenPackage->isChecked() && m_hooks.openPackage) {
		if (auto package = m_hooks.openPackage()) {
			layers << LayeredPackageReader::Layer {QStringLiteral("open-package"), m_hooks.openPackageLabel ? m_hooks.openPackageLabel() : QString(), package};
		}
	}
	auto work = m_work;
	work->cancel = false;
	{
		QMutexLocker locker(&work->mutex);
		work->phase = tr("Reviewing the release…");
		work->done = 0;
		work->total = 0;
	}
	auto* thread = QThread::create([=]() {
		const auto cancelled = [work]() { return work->cancel.load() || work->discarded.load(); };
		const auto report = [work](const QString& phase, qint64 done, qint64 total) {
			QMutexLocker locker(&work->mutex);
			work->phase = phase;
			work->done = done;
			work->total = total;
		};
		PackageReadControl control;
		control.isCancelled = cancelled;
		ReleaseStockContext stockContext = stock;
		if (outcome->withStock) {
			report(tr("Loading the game's asset index…"), 0, 0);
			outcome->stock = prepareReleaseStock(hasInstallation ? &installation : nullptr, release, gameKey, QString(), control);
			stockContext = outcome->stock;
		}
		if (outcome->withCatalog && !manifest.rootPath.isEmpty()) {
			report(tr("Reading the project…"), 0, 0);
			ProjectContentOptions options;
			options.isCancelled = cancelled;
			outcome->catalog = releaseCatalog(manifest, gameKey, options);
		}
		if (cancelled()) {
			return;
		}
		ReleaseRequest request;
		request.manifest = manifest;
		request.gameKey = gameKey;
		request.scope = scope;
		request.items = items;
		request.release = release;
		request.stock = stockContext.stock;
		request.extraLayers = layers;
		request.contentOptions.isCancelled = cancelled;
		request.progress = [report, cancelled](const QString& phase, int done, int total) {
			report(phase, done, total);
			return !cancelled();
		};
		outcome->plan = planRelease(request);
		outcome->plan.stockDescription = stockContext.description;
		for (const QString& warning : stockContext.warnings) {
			if (!outcome->plan.warnings.contains(warning)) {
				outcome->plan.warnings.prepend(warning);
			}
		}
		if (cancelled() || !outcome->plan.canPublish()) {
			return;
		}
		report(tr("Writing the release notes…"), 0, 0);
		const QString root = manifest.rootPath;
		if (!root.isEmpty()) {
			const QString changelogPath = QDir::isAbsolutePath(release.changelogFile) ? release.changelogFile : QDir(root).absoluteFilePath(release.changelogFile);
			QString error;
			if (!loadProjectChangelog(changelogPath, release.title, &outcome->changelog, &error)) {
				outcome->error = error;
			}
		}
		QVector<ReleaseRecordFile> inventory;
		QString error;
		if (!releaseInventory(outcome->plan, &inventory, &error, cancelled)) {
			outcome->error = error;
			return;
		}
		if (!root.isEmpty()) {
			if (const auto previous = latestReleaseRecord(root, release.version)) {
				outcome->diff = diffReleaseInventories(*previous, inventory);
			}
		}
		outcome->changes = outcome->changelog.unreleasedEntries();
		if (outcome->changes.isEmpty()) {
			outcome->changes = suggestedChangelogEntries(outcome->diff);
			outcome->additions = outcome->changes;
		}
		const ReleaseNotesInput input = releaseNotesInput(outcome->plan, outcome->changes, outcome->diff, QDate::currentDate());
		outcome->notes = releaseNotesMarkdown(input);
		outcome->readme = releaseReadmeText(input);
	});
	work->thread = thread;
	ownWorker(thread, work);
	connect(thread, &QThread::finished, this, [this, outcome]() {
		m_work->thread = nullptr;
		m_progressTimer->stop();
		if (m_work->planPending) {
			m_work->planPending = false;
			startPlan();
			return;
		}
		applyPlan(outcome);
	});
	m_state->setState(OperationState::Running, tr("Reviewing"));
	m_progress->setRange(0, 0);
	m_progress->setVisible(true);
	m_cancel->setEnabled(true);
	m_progressTimer->start();
	thread->start();
	// Publish now waits for this review, and says so.
	refreshPublishState();
}

void ReleaseDialog::applyPlan(const std::shared_ptr<PlanOutcome>& outcome)
{
	m_progress->setVisible(false);
	m_cancel->setEnabled(false);
	if (outcome->generation != m_generation) {
		return;
	}
	if (outcome->withStock) {
		m_stock = outcome->stock;
		showStockNotice();
	}
	if (outcome->withCatalog) {
		m_catalog = outcome->catalog;
		m_catalogLoaded = true;
		populateItems();
	}
	if (m_work->cancel && outcome->plan.entries.isEmpty() && outcome->plan.problems.isEmpty()) {
		m_state->setState(OperationState::Cancelled, tr("Review cancelled"));
		m_status->setText(tr("The review was cancelled. Change a choice to review again."));
		return;
	}
	m_plan = outcome->plan;
	m_changelog = outcome->changelog;
	m_changes = outcome->changes;
	m_additions = outcome->additions;
	m_diff = outcome->diff;

	const int blocking = m_plan.blockingCount();
	const int advisory = int(m_plan.problems.size()) - blocking;
	m_state->setState(m_plan.canPublish() ? (advisory > 0 || m_plan.overrideCount > 0 ? OperationState::Warning : OperationState::Completed) : OperationState::Failed,
		m_plan.canPublish() ? tr("Ready to publish") : tr("Not ready"));
	m_state->setDetail(m_plan.canPublish()
		? tr("%1 in %2").arg(tr("%n file(s)", nullptr, int(m_plan.entries.size())) + QStringLiteral(", ") + humanSize(m_plan.totalBytes), m_plan.packageFileName)
		: tr("%n blocking problem(s). See Problems.", nullptr, blocking));
	setStatusChip(m_includedChip, operationStateId(m_plan.entries.isEmpty() ? OperationState::Idle : OperationState::Completed),
		tr("Included: %1").arg(tr("%n file(s)", nullptr, int(m_plan.entries.size()))), m_plan.packageFileName);
	setStatusChip(m_stockChip, operationStateId(m_plan.stockChecked ? OperationState::Completed : OperationState::Warning),
		m_plan.stockChecked ? tr("From the game: %1").arg(m_plan.stock.size()) : tr("Game files: not checked"),
		m_plan.stockDescription.isEmpty() ? tr("The game's own files were not checked.") : m_plan.stockDescription);
	setStatusChip(m_overrideChip, operationStateId(m_plan.overrideCount > 0 ? OperationState::Warning : OperationState::Completed),
		tr("Replaces: %1").arg(m_plan.overrideCount), tr("Files with the same path as one of the game's own."));
	setStatusChip(m_problemChip, operationStateId(blocking > 0 ? OperationState::Failed : advisory > 0 ? OperationState::Warning : OperationState::Completed),
		blocking > 0 ? tr("Blocking: %1").arg(blocking) : tr("Problems: %1").arg(advisory));

	QVector<StudioChartSlice> slices;
	int pattern = 0;
	for (const ReleaseCompositionRow& row : m_plan.composition()) {
		StudioChartSlice slice;
		slice.id = row.role;
		slice.label = releaseRoleDisplayName(row.role);
		slice.value = double(row.bytes);
		slice.valueText = humanSize(row.bytes);
		slice.detail = tr("%n file(s)", nullptr, row.count);
		slice.patternIndex = pattern++;
		slices << slice;
	}
	m_chart->setSlices(slices);

	m_included->clear();
	for (const ReleaseEntry& entry : std::as_const(m_plan.entries)) {
		auto* item = new QTreeWidgetItem({entry.virtualPath, releaseRoleDisplayName(entry.role), humanSize(entry.sizeBytes), entry.requiredBy.join(QStringLiteral(", "))});
		QString tip = entry.sourcePath.isEmpty() ? entry.note : nativePath(entry.sourcePath);
		if (entry.replacesStock) {
			item->setIcon(0, studioIcon(QStringLiteral("warning"), StudioIconTone::Warning));
			tip += QLatin1Char('\n') + tr("Replaces the game's own file in %1.").arg(entry.stockSource);
		} else if (entry.loose) {
			item->setIcon(0, studioIcon(QStringLiteral("file"), StudioIconTone::Muted));
			tip += QLatin1Char('\n') + tr("Ships beside the package, not inside it.");
		}
		item->setToolTip(0, tip);
		item->setTextAlignment(2, Qt::AlignTrailing | Qt::AlignVCenter);
		m_included->addTopLevelItem(item);
	}
	fitColumns(m_included);
	m_provided->clear();
	for (const ReleaseReference& reference : std::as_const(m_plan.stock)) {
		auto* item = new QTreeWidgetItem({reference.reference, reference.source, reference.path, reference.requiredBy.join(QStringLiteral(", "))});
		if (reference.identicalCopy) {
			item->setToolTip(0, tr("The project holds an identical copy; the game's own is used."));
		}
		m_provided->addTopLevelItem(item);
	}
	fitColumns(m_provided);
	m_problems->clear();
	for (const ReleaseProblem& problem : std::as_const(m_plan.problems)) {
		auto* item = new QTreeWidgetItem({problem.message, problem.reference, problem.requiredBy.join(QStringLiteral(", "))});
		item->setIcon(0, studioIcon(problem.blocking ? QStringLiteral("error") : QStringLiteral("warning"), problem.blocking ? StudioIconTone::Danger : StudioIconTone::Warning));
		item->setData(0, Qt::UserRole, problem.kind);
		item->setData(0, Qt::UserRole + 1, problem.reference);
		item->setData(0, Qt::AccessibleTextRole, (problem.blocking ? tr("Blocking: %1") : tr("Advisory: %1")).arg(problem.message));
		item->setToolTip(0, problem.message);
		m_problems->addTopLevelItem(item);
	}
	for (const QString& warning : std::as_const(m_plan.warnings)) {
		auto* item = new QTreeWidgetItem({warning, QString(), QString()});
		item->setIcon(0, studioIcon(QStringLiteral("info"), StudioIconTone::Muted));
		item->setToolTip(0, warning);
		m_problems->addTopLevelItem(item);
	}
	setPanelTabText(m_tabs, m_tabs->indexOf(m_problems), blocking + advisory > 0 ? tr("Problems (%1)").arg(blocking + advisory) : tr("Problems"));
	m_details->setPlainText(releasePlanText(m_plan));

	refreshUnreleased();
	m_settingText = true;
	if (!m_notesEdited) {
		m_notes->setPlainText(outcome->notes);
	}
	if (!m_readmeEdited) {
		m_readme->setPlainText(outcome->readme);
	}
	m_settingText = false;
	if (!outcome->error.isEmpty()) {
		m_status->setText(outcome->error);
	} else if (m_plan.canPublish()) {
		m_status->setText(m_plan.stockChecked ? tr("Review the files and notes, then publish.")
											  : tr("The game's own files were not checked; anything the project lacks is assumed to come with the game."));
	} else {
		m_status->setText(tr("Resolve the blocking problems to publish."));
	}
	if (blocking > 0) {
		m_tabs->setCurrentWidget(m_problems);
	}
	refreshVersionState();
	refreshPublishState();
}

void ReleaseDialog::showStockNotice()
{
	if (m_stock.available && m_stock.warnings.isEmpty()) {
		m_notice->dismiss();
		return;
	}
	const QString gameName = gameDefinitionForKey(m_gameKey).displayName;
	if (!m_hasInstallation) {
		m_notice->showNotice(operationStateId(OperationState::Warning), tr("Your game's files are unknown"),
			tr("Link a %1 installation to the project on the Workspace page so the release can leave the game's own files out and spot missing ones.").arg(gameName));
		return;
	}
	const bool stale = m_stock.status.loaded && !m_stock.status.fresh;
	m_notice->showNotice(operationStateId(OperationState::Warning),
		stale ? tr("The game's asset index is out of date") : !m_stock.available ? tr("Index the game's assets") : tr("Check the game's asset index"),
		stale ? tr("%1 changed since it was indexed. Index it again for an exact check.").arg(m_installation.displayName)
			  : !m_stock.available ? tr("VibeStudio needs to read %1's own packages once to tell its files from yours. Nothing in the installation is changed.").arg(m_installation.displayName)
								   : m_stock.warnings.join(QLatin1Char(' ')));
	if (m_hooks.indexInstallation) {
		QPushButton* index = m_notice->addAction(stale ? tr("Index Again") : tr("Index Game Assets"), QStringLiteral("index"), true);
		index->setObjectName(QStringLiteral("releaseIndexGame"));
		connect(index, &QPushButton::clicked, this, [this, index]() {
			index->setEnabled(false);
			m_status->setText(tr("Indexing %1. Activity shows the progress.").arg(m_installation.displayName));
			// Indexing outlives neither guard: the dialog can close, and a new
			// notice replaces this button.
			const QPointer<ReleaseDialog> dialog(this);
			const QPointer<QPushButton> button(index);
			m_hooks.indexInstallation(m_installation.id, [dialog, button](bool succeeded) {
				if (!dialog) {
					return;
				}
				if (!succeeded) {
					if (button) {
						button->setEnabled(true);
					}
					dialog->m_status->setText(tr("The game's assets were not indexed. Activity has the details."));
					return;
				}
				dialog->m_stock = {};
				dialog->m_notice->dismiss();
				dialog->startPlan();
			});
		});
	}
}

void ReleaseDialog::regenerateNotes()
{
	if (!m_plan.canPublish()) {
		return;
	}
	ReleasePlan plan = m_plan;
	plan.release = releaseFromFields();
	const ReleaseNotesInput input = releaseNotesInput(plan, m_changes, m_diff, QDate::currentDate());
	m_settingText = true;
	m_notes->setPlainText(releaseNotesMarkdown(input));
	m_readme->setPlainText(releaseReadmeText(input));
	m_settingText = false;
}

void ReleaseDialog::addChangelogEntry()
{
	const QString text = m_changeText->text().trimmed();
	if (text.isEmpty()) {
		return;
	}
	if (m_manifest.rootPath.isEmpty()) {
		m_status->setText(tr("Open a project to keep a changelog."));
		return;
	}
	const ProjectReleaseSettings release = releaseFromFields();
	const QString path = QDir::isAbsolutePath(release.changelogFile) ? release.changelogFile : QDir(m_manifest.rootPath).absoluteFilePath(release.changelogFile);
	ProjectChangelog changelog;
	QString error;
	if (!loadProjectChangelog(path, release.title, &changelog, &error)
		|| !vibestudio::addChangelogEntry(&changelog, m_changeCategory->currentData().toString(), text, &error)
		|| !saveProjectChangelog(changelog, &error)) {
		m_status->setText(error);
		return;
	}
	m_changeText->clear();
	m_changelog = changelog;
	m_changes = changelog.unreleasedEntries();
	m_additions.clear();
	m_status->setText(tr("Recorded in %1.").arg(nativePath(path)));
	refreshUnreleased();
	if (!m_notesEdited) {
		regenerateNotes();
	}
}

void ReleaseDialog::refreshUnreleased()
{
	m_unreleased->clear();
	for (const ChangelogEntry& entry : std::as_const(m_changes)) {
		auto* item = new QListWidgetItem(QStringLiteral("%1: %2").arg(changelogCategoryDisplayName(entry.category), entry.text));
		if (!m_additions.isEmpty()) {
			item->setToolTip(tr("Suggested from what changed; it is added to the changelog when you publish."));
			item->setIcon(studioIcon(QStringLiteral("sparkle"), StudioIconTone::Muted));
		}
		m_unreleased->addItem(item);
	}
	if (m_changes.isEmpty()) {
		auto* empty = new QListWidgetItem(tr("Nothing recorded since the last release."));
		empty->setFlags(Qt::NoItemFlags);
		m_unreleased->addItem(empty);
	}
}

void ReleaseDialog::refreshVersionState()
{
	const QString version = m_version->text().trimmed();
	QMenu* menu = m_nextVersion->menu();
	menu->clear();
	for (const auto& [part, label] : {std::pair {QStringLiteral("patch"), tr("Patch: %1")}, std::pair {QStringLiteral("minor"), tr("Minor: %1")},
			 std::pair {QStringLiteral("major"), tr("Major: %1")}}) {
		const QString next = bumpReleaseVersion(version.isEmpty() ? QStringLiteral("1.0.0") : version, part);
		menu->addAction(label.arg(next), this, [this, next]() {
			m_version->setText(next);
			refreshVersionState();
			schedulePlan();
		});
	}
	QString state;
	if (!isValidReleaseVersion(version)) {
		state = tr("Use letters, digits, '.', '-', '+' or '_' for the version, such as 1.2.0.");
	} else if (!m_manifest.rootPath.isEmpty() && QFileInfo::exists(releaseRecordPath(m_manifest.rootPath, version)) && !m_replace->isChecked()) {
		state = tr("%1 has already been released. Choose the next version, or replace that release.").arg(version);
	}
	m_versionState->setText(state);
	m_versionState->setVisible(!state.isEmpty());
	refreshPublishState();
}

void ReleaseDialog::refreshPublishState()
{
	const QString version = m_version->text().trimmed();
	const bool released = !m_manifest.rootPath.isEmpty() && QFileInfo::exists(releaseRecordPath(m_manifest.rootPath, version)) && !m_replace->isChecked();
	// What stops publishing, first things first. A pending review would publish
	// yesterday's choices, so it waits for that too.
	QString blocked;
	if (m_busy || m_work->thread || m_planTimer->isActive()) {
		blocked = tr("Wait for the review to finish.");
	} else if (!m_plan.canPublish()) {
		blocked = tr("Resolve the blocking problems first.");
	} else if (!isValidReleaseVersion(version)) {
		blocked = tr("Use letters, digits, '.', '-', '+' or '_' for the version, such as 1.2.0.");
	} else if (released) {
		blocked = tr("%1 has already been released. Choose the next version, or replace that release.").arg(version);
	} else if (m_output->text().trimmed().isEmpty()) {
		blocked = tr("Choose a folder for the release files");
	}
	m_publish->setEnabled(blocked.isEmpty());
	m_publish->setToolTip(blocked.isEmpty() ? tr("Write the package, readme, notes and archive, record the release and update the changelog (Ctrl+Enter).") : blocked);
	m_publish->setAccessibleDescription(blocked);
	m_updateChangelog->setEnabled(!m_manifest.rootPath.isEmpty());
}

void ReleaseDialog::setBusy(bool busy, const QString& status)
{
	m_busy = busy;
	for (QWidget* widget : {static_cast<QWidget*>(m_scope), static_cast<QWidget*>(m_items), static_cast<QWidget*>(m_format), static_cast<QWidget*>(m_title),
			 static_cast<QWidget*>(m_version), static_cast<QWidget*>(m_output), static_cast<QWidget*>(m_addChange)}) {
		widget->setEnabled(!busy);
	}
	m_cancel->setEnabled(busy);
	m_progress->setVisible(busy);
	if (!status.isEmpty()) {
		m_status->setText(status);
	}
	refreshPublishState();
}

void ReleaseDialog::publish()
{
	if (m_busy || m_work->thread || !m_publish->isEnabled()) {
		return;
	}
	ReleasePlan plan = m_plan;
	plan.release = releaseFromFields();
	plan.release.version = m_version->text().trimmed();
	// Version and descriptive fields do not change what ships, so the reviewed
	// plan stands; names and formats re-plan before Publish is enabled.
	if (m_hooks.saveReleaseSettings) {
		QString error;
		if (!m_hooks.saveReleaseSettings(plan.release, m_gameKey, &error)) {
			m_status->setText(tr("The release settings could not be saved: %1").arg(error));
			return;
		}
	}
	ReleasePublishRequest request;
	request.plan = plan;
	request.projectRoot = m_manifest.rootPath;
	request.outputDirectory = QDir::cleanPath(QDir::fromNativeSeparators(m_output->text().trimmed()));
	request.notesMarkdown = m_notes->toPlainText();
	request.readmeText = m_readme->toPlainText();
	request.writeArchive = m_archive->isChecked();
	request.updateChangelog = m_updateChangelog->isChecked() && !m_manifest.rootPath.isEmpty();
	request.changelog = m_changelog;
	request.changelogAdditions = m_additions;
	request.releaseDate = QDate::currentDate();
	DeflateLevel level = DeflateLevel::Default;
	deflateLevelFromId(m_compression->currentData().toString(), &level);
	request.compression = level;
	request.overwrite = m_replace->isChecked();
	const QString problem = releaseOutputProblem(plan, request.outputDirectory, request.overwrite);
	if (!problem.isEmpty()) {
		m_status->setText(problem);
		return;
	}
	auto work = m_work;
	work->cancel = false;
	request.isCancelled = [work]() { return work->cancel.load(); };
	request.progress = [work](const QString& phase, qint64 done, qint64 total) {
		QMutexLocker locker(&work->mutex);
		work->phase = phase;
		work->done = done;
		work->total = total;
	};
	m_task = m_hooks.beginTask ? m_hooks.beginTask(tr("Publish %1 %2").arg(plan.release.title, plan.release.version), nativePath(request.outputDirectory)) : QString();
	auto result = std::make_shared<ReleasePublishResult>();
	auto* thread = QThread::create([request, result]() { *result = publishRelease(request); });
	work->thread = thread;
	ownWorker(thread, work);
	connect(thread, &QThread::finished, this, [this, result]() {
		m_work->thread = nullptr;
		m_progressTimer->stop();
		finishPublish(*result);
	});
	setBusy(true, tr("Publishing…"));
	m_state->setState(OperationState::Running, tr("Publishing"));
	m_progressTimer->start();
	thread->start();
}

void ReleaseDialog::finishPublish(const ReleasePublishResult& result)
{
	m_result = result;
	setBusy(false, QString());
	if (m_hooks.endTask && !m_task.isEmpty()) {
		m_hooks.endTask(m_task, result.succeeded, result.cancelled, result.succeeded ? nativePath(result.packagePath) : result.error);
		m_task.clear();
	}
	if (!result.succeeded) {
		m_state->setState(result.cancelled ? OperationState::Cancelled : OperationState::Failed, result.cancelled ? tr("Cancelled") : tr("Publishing failed"));
		m_status->setText(releasePublishResultText(result));
		m_notice->showNotice(operationStateId(result.cancelled ? OperationState::Cancelled : OperationState::Failed),
			result.cancelled ? tr("Publishing was cancelled") : tr("The release was not published"), result.error);
		return;
	}
	m_state->setState(OperationState::Completed, tr("Published"));
	m_state->setDetail(nativePath(result.packagePath));
	m_status->setText(tr("Released %1 %2: %3, %4.").arg(m_plan.release.title, m_version->text().trimmed(), QFileInfo(result.packagePath).fileName(), humanSize(result.packageBytes)));
	m_notice->showNotice(operationStateId(OperationState::Completed), tr("Released %1 %2").arg(m_title->text().trimmed(), m_version->text().trimmed()),
		tr("The package, readme, notes and archive are in %1.").arg(nativePath(result.outputDirectory)));
	if (m_hooks.reveal) {
		QPushButton* show = m_notice->addAction(tr("Show Folder"), QStringLiteral("reveal"), true);
		connect(show, &QPushButton::clicked, this, [this]() { m_hooks.reveal(m_result.outputDirectory); });
	}
	if (m_hooks.openInPackages) {
		QPushButton* open = m_notice->addAction(tr("Open Package"), QStringLiteral("package"));
		connect(open, &QPushButton::clicked, this, [this]() { m_hooks.openInPackages(m_result.packagePath); });
	}
	m_reveal->setVisible(true);
	// The Unreleased section now sits under this version.
	if (!result.changelogPath.isEmpty()) {
		ProjectChangelog changelog;
		if (loadProjectChangelog(result.changelogPath, m_plan.release.title, &changelog)) {
			m_changelog = changelog;
			m_changes = changelog.unreleasedEntries();
			m_additions.clear();
			refreshUnreleased();
		}
	}
	if (m_hooks.published) {
		m_hooks.published(result);
	}
	refreshVersionState();
}

void ReleaseDialog::chooseOutput()
{
	const QString path = QFileDialog::getExistingDirectory(this, tr("Release Output Folder"), m_output->text());
	if (!path.isEmpty()) {
		m_output->setText(nativePath(path));
		refreshPublishState();
	}
}

void ReleaseDialog::addItems()
{
	const ReleaseScope scope = currentScope();
	QStringList chosen;
	if (scope == ReleaseScope::Textures) {
		const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a Texture Folder"), m_manifest.rootPath);
		if (!folder.isEmpty()) {
			chosen << folder;
		}
	} else {
		const QString filter = scope == ReleaseScope::Maps ? tr("Maps (*.map *.bsp *.wad)") : tr("Models (*.md3 *.mdl *.md2 *.iqm *.md5mesh *.mdc *.mdr *.ase *.lwo)");
		chosen = QFileDialog::getOpenFileNames(this, tr("Add Files to Release"), m_manifest.rootPath, filter);
	}
	for (const QString& path : std::as_const(chosen)) {
		const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
		if (!m_extraItems.contains(clean)) {
			m_extraItems << clean;
		}
		m_pendingItems << clean;
	}
	if (!chosen.isEmpty()) {
		populateItems();
		schedulePlan();
	}
}

void ReleaseDialog::activateProblem(QTreeWidgetItem* item)
{
	if (!item) {
		return;
	}
	const QString kind = item->data(0, Qt::UserRole).toString();
	if ((kind == QStringLiteral("unbuilt-map") || kind == QStringLiteral("stale-map")) && m_hooks.buildMap) {
		const QString name = item->data(0, Qt::UserRole + 1).toString();
		for (const ReleaseMapInfo& map : std::as_const(m_plan.maps)) {
			if (map.name == name && !map.sourcePath.isEmpty()) {
				m_hooks.buildMap(map.sourcePath);
				return;
			}
		}
	}
}

void ReleaseDialog::reject()
{
	if (m_busy) {
		// Publishing finishes or cancels before the dialog goes.
		m_work->cancel = true;
		m_status->setText(tr("Cancelling before closing…"));
		return;
	}
	m_work->cancel = true;
	QDialog::reject();
}

void ReleaseDialog::keyPressEvent(QKeyEvent* event)
{
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && (event->modifiers() & Qt::ControlModifier)) {
		publish();
		return;
	}
	QDialog::keyPressEvent(event);
}

bool ReleaseDialog::busy() const
{
	return m_busy || m_work->thread != nullptr || m_planTimer->isActive();
}

const ReleasePlan& ReleaseDialog::plan() const
{
	return m_plan;
}

const ReleasePublishResult& ReleaseDialog::lastResult() const
{
	return m_result;
}

QString ReleaseDialog::notesText() const
{
	return m_notes->toPlainText();
}

QString ReleaseDialog::readmeText() const
{
	return m_readme->toPlainText();
}

QString ReleaseDialog::outputDirectory() const
{
	return QDir::cleanPath(QDir::fromNativeSeparators(m_output->text().trimmed()));
}

void ReleaseDialog::setOutputDirectory(const QString& path)
{
	m_output->setText(nativePath(path));
	refreshPublishState();
}

} // namespace vibestudio
