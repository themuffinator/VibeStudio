#include "app/level_ai_edit_dialog.h"

#include "core/studio_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

enum Column {
	EditColumn,
	StateColumn,
	NotesColumn,
};

QPlainTextEdit* readOnlyText(const QString& objectName, const QString& accessibleName)
{
	auto* text = new QPlainTextEdit;
	text->setObjectName(objectName);
	text->setAccessibleName(accessibleName);
	text->setReadOnly(true);
	text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	return text;
}

QString documentKey(const LevelMapDocument& document)
{
	return document.sourcePath + QLatin1Char('|') + document.mapName;
}

} // namespace

LevelAiEditDialog::LevelAiEditDialog(QWidget* parent, LevelAiEditDialogHooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
	, m_client(std::make_unique<AiChatClient>())
{
	setObjectName(QStringLiteral("levelAiEditDialog"));
	setWindowTitle(tr("Edit Map with AI"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Say what should change in the open map; review the edits the text model proposes, then apply the ones you keep."));
	resize(980, 700);

	auto* root = new QVBoxLayout(this);
	m_mapStatus = new QLabel;
	m_mapStatus->setObjectName(QStringLiteral("levelAiEditMapStatus"));
	m_mapStatus->setAccessibleName(tr("Open map"));
	m_mapStatus->setWordWrap(true);
	m_mapStatus->setTextFormat(Qt::PlainText);
	root->addWidget(m_mapStatus);

	auto* instructionLabel = new QLabel(tr("&What should change?"));
	m_instruction = new QPlainTextEdit;
	m_instruction->setObjectName(QStringLiteral("levelAiEditInstruction"));
	m_instruction->setAccessibleName(tr("Edit instruction"));
	m_instruction->setPlaceholderText(tr("For example: add a light above each player start, or retexture the selected brushes with a metal texture"));
	m_instruction->setTabChangesFocus(true);
	m_instruction->setMaximumHeight(90);
	instructionLabel->setBuddy(m_instruction);
	root->addWidget(instructionLabel);
	root->addWidget(m_instruction);

	auto* connectionRow = new QHBoxLayout;
	m_connectionStatus = new QLabel;
	m_connectionStatus->setObjectName(QStringLiteral("levelAiEditConnection"));
	m_connectionStatus->setAccessibleName(tr("Text model status"));
	m_connectionStatus->setWordWrap(true);
	m_connectionStatus->setTextFormat(Qt::PlainText);
	m_previewRequest = new QPushButton(tr("Preview Re&quest…"));
	m_previewRequest->setAccessibleName(tr("Preview the edit request"));
	m_previewRequest->setToolTip(tr("Show exactly what would be sent, the map summary included, without sending it."));
	m_loadProposal = new QPushButton(tr("&Load Proposal…"));
	m_loadProposal->setObjectName(QStringLiteral("levelAiEditLoad"));
	m_loadProposal->setAccessibleName(tr("Load a saved proposal"));
	m_loadProposal->setToolTip(tr("Review a proposal saved earlier, or one from vibestudio --cli map ai-edit --save-proposal. No model is asked."));
	connectionRow->addWidget(m_connectionStatus, 1);
	connectionRow->addWidget(m_previewRequest);
	connectionRow->addWidget(m_loadProposal);
	root->addLayout(connectionRow);

	auto* askRow = new QHBoxLayout;
	m_ask = new QPushButton(tr("&Ask for Edits"));
	m_ask->setObjectName(QStringLiteral("levelAiEditAsk"));
	m_ask->setAccessibleName(tr("Ask the text model for edits"));
	m_ask->setToolTip(tr("Send the instruction and a summary of the map; nothing changes until you apply (Ctrl+Enter)."));
	m_ask->setDefault(true);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName(QStringLiteral("levelAiEditCancel"));
	m_cancel->setAccessibleName(tr("Cancel the request"));
	m_cancel->setEnabled(false);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Request progress"));
	m_progress->setRange(0, 0);
	m_progress->setVisible(false);
	askRow->addWidget(m_ask);
	askRow->addWidget(m_cancel);
	askRow->addStretch(1);
	root->addLayout(askRow);
	m_status = new QLabel(tr("Say what should change, then ask. You review every edit before it is made."));
	m_status->setObjectName(QStringLiteral("levelAiEditStatus"));
	m_status->setAccessibleName(tr("Edit status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	root->addWidget(m_status);
	root->addWidget(m_progress);

	auto* splitter = new QSplitter(Qt::Vertical);
	splitter->setChildrenCollapsible(false);
	auto* review = new QWidget;
	auto* reviewLayout = new QVBoxLayout(review);
	reviewLayout->setContentsMargins(0, 0, 0, 0);
	m_summary = new QLabel;
	m_summary->setObjectName(QStringLiteral("levelAiEditSummary"));
	m_summary->setAccessibleName(tr("Proposal summary"));
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	reviewLayout->addWidget(m_summary);
	m_actions = new QTreeWidget;
	m_actions->setObjectName(QStringLiteral("levelAiEditActions"));
	m_actions->setAccessibleName(tr("Proposed edits"));
	m_actions->setAccessibleDescription(tr("Each edit with its state; checked edits are applied. Space checks or unchecks the current edit."));
	m_actions->setHeaderLabels({tr("Edit"), tr("State"), tr("Notes")});
	m_actions->setRootIsDecorated(false);
	m_actions->setUniformRowHeights(true);
	m_actions->setWordWrap(true);
	m_actions->header()->setStretchLastSection(true);
	m_actions->setColumnWidth(EditColumn, 430);
	m_actions->setColumnWidth(StateColumn, 90);
	reviewLayout->addWidget(m_actions, 1);
	auto* applyRow = new QHBoxLayout;
	m_apply = new QPushButton(tr("A&pply Checked"));
	m_apply->setObjectName(QStringLiteral("levelAiEditApply"));
	m_apply->setAccessibleName(tr("Apply the checked edits to the map"));
	m_apply->setToolTip(tr("Each edit becomes its own undo step in Levels."));
	m_saveProposal = new QPushButton(tr("&Save Proposal…"));
	m_saveProposal->setObjectName(QStringLiteral("levelAiEditSave"));
	m_saveProposal->setAccessibleName(tr("Save the proposal as JSON"));
	m_saveProposal->setToolTip(tr("Keep the edits and your choices to review later or apply with vibestudio --cli map ai-edit --proposal."));
	m_details = new QToolButton;
	m_details->setText(tr("&Details"));
	m_details->setAccessibleName(tr("Show the map summary, the answer, and the results"));
	m_details->setCheckable(true);
	m_details->setArrowType(Qt::RightArrow);
	m_details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	applyRow->addWidget(m_apply);
	applyRow->addWidget(m_saveProposal);
	applyRow->addStretch(1);
	applyRow->addWidget(m_details);
	reviewLayout->addLayout(applyRow);
	splitter->addWidget(review);
	m_detailTabs = new QTabWidget;
	m_detailTabs->setAccessibleName(tr("Edit details"));
	m_contextText = readOnlyText(QStringLiteral("levelAiEditContext"), tr("Map summary as sent"));
	m_answerText = readOnlyText(QStringLiteral("levelAiEditAnswer"), tr("The model's answer"));
	m_resultText = readOnlyText(QStringLiteral("levelAiEditResults"), tr("What was applied"));
	for (QPlainTextEdit* text : {m_contextText, m_answerText, m_resultText}) {
		text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	}
	m_detailTabs->addTab(m_contextText, tr("Map Summary"));
	m_detailTabs->addTab(m_answerText, tr("Answer"));
	m_detailTabs->addTab(m_resultText, tr("Results"));
	m_detailTabs->setVisible(false);
	splitter->addWidget(m_detailTabs);
	root->addWidget(splitter, 1);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &LevelAiEditDialog::reject);
	connect(m_ask, &QPushButton::clicked, this, &LevelAiEditDialog::ask);
	connect(m_cancel, &QPushButton::clicked, this, &LevelAiEditDialog::cancelRequest);
	connect(m_previewRequest, &QPushButton::clicked, this, &LevelAiEditDialog::showRequestPreview);
	connect(m_loadProposal, &QPushButton::clicked, this, &LevelAiEditDialog::chooseProposalFile);
	connect(m_apply, &QPushButton::clicked, this, &LevelAiEditDialog::applyChecked);
	connect(m_saveProposal, &QPushButton::clicked, this, &LevelAiEditDialog::saveProposal);
	connect(m_actions, &QTreeWidget::itemChanged, this, [this] { updateButtons(); });
	connect(m_details, &QToolButton::toggled, this, [this](bool shown) {
		m_detailTabs->setVisible(shown);
		m_details->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
	});
	auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	connect(shortcut, &QShortcut::activated, this, &LevelAiEditDialog::ask);
	refreshStatus();
}

LevelAiEditDialog::~LevelAiEditDialog() = default;

void LevelAiEditDialog::setInstruction(const QString& instruction)
{
	m_instruction->setPlainText(instruction);
}

bool LevelAiEditDialog::busy() const
{
	return m_busy;
}

const LevelAiEditProposal& LevelAiEditDialog::proposal() const
{
	return m_proposal;
}

const LevelMapDocument* LevelAiEditDialog::editableDocument() const
{
	const LevelMapDocument* document = m_hooks.document ? m_hooks.document() : nullptr;
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return nullptr;
	}
	return document;
}

void LevelAiEditDialog::refreshStatus()
{
	const LevelMapDocument* any = m_hooks.document ? m_hooks.document() : nullptr;
	const LevelMapDocument* document = editableDocument();
	if (!document) {
		m_mapStatus->setText(any && any->format == LevelMapFormat::DoomWad
				? tr("The open map is a Doom map. Edit with AI works on Quake, Quake II, and Quake III .map files; Doom sectors and things have their own tools in Levels.")
				: tr("Open a Quake, Quake II, or Quake III .map in Levels to edit it here."));
	} else {
		QStringList selected;
		for (const LevelMapSelectionRef& ref : document->selection) {
			selected << levelMapSelectionRefId(ref);
		}
		QString selection = tr("nothing");
		if (selected.size() > 8) {
			selection = tr("%1 and %2 more").arg(selected.mid(0, 8).join(QStringLiteral(", "))).arg(selected.size() - 8);
		} else if (!selected.isEmpty()) {
			selection = selected.join(QStringLiteral(", "));
		}
		const QString name = !document->sourcePath.isEmpty() ? QFileInfo(document->sourcePath).fileName()
			: !document->mapName.isEmpty()					 ? document->mapName
															 : tr("the unsaved map");
		m_mapStatus->setText(tr("Editing %1 (entities: %2, brushes: %3). Selected: %4. A request can name \"the selected\" objects.")
								 .arg(name)
								 .arg(document->entities.size())
								 .arg(document->brushes.size())
								 .arg(selection));
	}
	m_mapStatus->setAccessibleDescription(m_mapStatus->text());
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	m_connectionStatus->setText(aiTextConnectionBlockText(connection));
	m_connectionStatus->setAccessibleDescription(m_connectionStatus->text());
	updateButtons();
}

void LevelAiEditDialog::updateButtons()
{
	const bool mapOpen = editableDocument() != nullptr;
	m_ask->setEnabled(!m_busy && mapOpen);
	m_previewRequest->setEnabled(!m_busy && mapOpen);
	m_loadProposal->setEnabled(!m_busy && mapOpen);
	m_cancel->setEnabled(m_busy);
	bool anyChecked = false;
	for (int row = 0; row < m_actions->topLevelItemCount(); ++row) {
		const QTreeWidgetItem* item = m_actions->topLevelItem(row);
		anyChecked = anyChecked || ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(EditColumn) == Qt::Checked);
	}
	m_apply->setEnabled(!m_busy && mapOpen && !m_applied && anyChecked);
	m_saveProposal->setEnabled(!m_busy && !m_proposal.actions.isEmpty());
}

void LevelAiEditDialog::setBusy(bool busy, const QString& status)
{
	m_busy = busy;
	m_status->setText(status);
	m_progress->setVisible(busy);
	updateButtons();
}

void LevelAiEditDialog::finishTask(bool succeeded, bool cancelled, const QString& summary)
{
	if (!m_task.isEmpty() && m_hooks.endTask) {
		m_hooks.endTask(m_task, succeeded, cancelled, summary);
	}
	m_task.clear();
}

void LevelAiEditDialog::ask()
{
	if (m_busy) {
		return;
	}
	refreshStatus();
	const LevelMapDocument* document = editableDocument();
	if (!document) {
		m_status->setText(m_mapStatus->text());
		return;
	}
	const QString instruction = m_instruction->toPlainText().trimmed();
	if (instruction.isEmpty()) {
		m_status->setText(tr("Say what should change in the map."));
		m_instruction->setFocus();
		return;
	}
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	if (!connection.ready()) {
		m_status->setText(aiTextConnectionBlockText(connection));
		return;
	}
	const QString projectRoot = m_hooks.projectRoot ? m_hooks.projectRoot() : QString();
	const AiChatRequest request = levelAiEditRequest(*document, instruction, connection.connectorId, connection.model, connection.endpoint, projectRoot, QDir::homePath());
	m_contextText->setPlainText(request.messages.value(0).text);
	if (!connection.local && m_hooks.confirmSend) {
		AiHttpRequest http;
		QString error;
		if (!buildAiHttpRequest(request, QString(), &http, &error)) {
			m_status->setText(error);
			return;
		}
		if (!m_hooks.confirmSend(connection.connectorId, connection.displayName, connection.endpoint, http)) {
			m_status->setText(tr("Nothing was sent."));
			return;
		}
	}
	if (m_hooks.beginTask) {
		m_task = m_hooks.beginTask(tr("Edit Map with AI"), tr("Asking %1 (%2): %3").arg(connection.displayName, connection.model, instruction.left(120)));
	}
	setBusy(true, tr("Asking %1 for edits…").arg(connection.displayName));
	const QString key = documentKey(*document);
	QString error;
	const bool started = m_client->send(request, aiTextConnectionApiKey(connection), [this, key](const AiChatResponse& response) {
		if (!response.ok) {
			const bool cancelled = response.failure == AiChatFailure::Cancelled;
			const QString message = cancelled ? tr("Cancelled. Nothing was changed.") : tr("The text model did not answer: %1").arg(response.errorMessage);
			setBusy(false, message);
			finishTask(false, cancelled, message);
			return;
		}
		m_answerText->setPlainText(response.text);
		LevelAiEditProposal proposal;
		QString readError;
		if (!levelAiEditProposalFromAnswer(response.text, &proposal, &readError)) {
			const QString message = tr("The text model's answer held no edits: %1 Its answer is under Details.").arg(readError);
			setBusy(false, message);
			finishTask(false, false, message);
			return;
		}
		setBusy(false, QString());
		const LevelMapDocument* now = editableDocument();
		if (!now || documentKey(*now) != key) {
			const QString message = tr("Another map was opened while the model answered; ask again for this one.");
			m_status->setText(message);
			finishTask(false, false, message);
			return;
		}
		showProposal(proposal);
		finishTask(true, false, m_status->text());
	}, &error);
	if (!started) {
		setBusy(false, error);
		finishTask(false, false, error);
	}
}

void LevelAiEditDialog::cancelRequest()
{
	if (m_busy && m_client->busy()) {
		m_client->cancel();
	}
}

void LevelAiEditDialog::showProposal(const LevelAiEditProposal& proposal)
{
	m_proposal = proposal;
	m_applied = false;
	const LevelMapDocument* document = editableDocument();
	if (document) {
		validateLevelAiEditProposal(*document, &m_proposal);
	}
	// Which map, as it was, the proposal was checked against.
	m_proposalMap = document ? documentKey(*document) : QString();
	m_proposalRevision = document ? document->revision : 0;
	m_summary->setText(m_proposal.summary.isEmpty() ? tr("The proposal has no summary.") : m_proposal.summary);
	const QSignalBlocker blocker(m_actions);
	m_actions->clear();
	int ready = 0;
	for (int index = 0; index < m_proposal.actions.size(); ++index) {
		const LevelAiEditAction& action = m_proposal.actions.at(index);
		auto* item = new QTreeWidgetItem(m_actions);
		item->setData(EditColumn, Qt::UserRole, index);
		item->setText(EditColumn, QStringLiteral("%1. %2").arg(index + 1).arg(action.description));
		if (action.valid()) {
			// Blocked edits keep no check box but stay reachable, so their
			// reasons can be read with the keyboard.
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(EditColumn, action.enabled ? Qt::Checked : Qt::Unchecked);
			item->setText(StateColumn, tr("Ready"));
			item->setText(NotesColumn, action.reason);
			++ready;
		} else {
			item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
			item->setText(StateColumn, tr("Blocked"));
			item->setText(NotesColumn, action.problems.join(QLatin1Char(' ')));
		}
		const QString detail = QStringLiteral("%1: %2").arg(item->text(StateColumn), item->text(NotesColumn));
		item->setToolTip(EditColumn, detail);
		item->setToolTip(NotesColumn, item->text(NotesColumn));
		item->setData(EditColumn, Qt::AccessibleDescriptionRole, detail);
	}
	if (!m_proposal.problems.isEmpty()) {
		m_answerText->appendPlainText(QStringLiteral("\n") + tr("Schema problems: %1").arg(m_proposal.problems.join(QLatin1Char(' '))));
	}
	if (m_proposal.actions.isEmpty()) {
		m_status->setText(tr("No edits were proposed. The summary above says why."));
	} else {
		m_status->setText(tr("%n edit(s) proposed, %1 ready. Uncheck any you do not want, then choose Apply Checked.", nullptr, int(m_proposal.actions.size())).arg(ready));
	}
	m_resultText->clear();
	updateButtons();
	if (m_actions->topLevelItemCount() > 0) {
		m_actions->setCurrentItem(m_actions->topLevelItem(0));
	}
}

LevelAiEditProposal LevelAiEditDialog::checkedProposal() const
{
	LevelAiEditProposal chosen = m_proposal;
	for (int row = 0; row < m_actions->topLevelItemCount(); ++row) {
		const QTreeWidgetItem* item = m_actions->topLevelItem(row);
		const int index = item->data(EditColumn, Qt::UserRole).toInt();
		if (index >= 0 && index < chosen.actions.size()) {
			chosen.actions[index].enabled = (item->flags() & Qt::ItemIsUserCheckable) && item->checkState(EditColumn) == Qt::Checked;
		}
	}
	return chosen;
}

void LevelAiEditDialog::applyChecked()
{
	if (m_busy || m_applied || !m_hooks.apply) {
		return;
	}
	const LevelMapDocument* document = editableDocument();
	if (!document || documentKey(*document) != m_proposalMap) {
		m_status->setText(tr("These edits were proposed for another map; ask again for the one open now."));
		return;
	}
	// The map may have changed since the proposal: check the chosen edits again.
	LevelAiEditProposal chosen = checkedProposal();
	const bool changed = m_proposalRevision != document->revision;
	QVector<bool> wanted;
	for (const LevelAiEditAction& action : chosen.actions) {
		wanted << action.enabled;
	}
	validateLevelAiEditProposal(*document, &chosen);
	int nowBlocked = 0;
	for (int index = 0; index < chosen.actions.size(); ++index) {
		nowBlocked += wanted.at(index) && !chosen.actions.at(index).valid() ? 1 : 0;
	}
	const LevelAiEditApplyReport report = m_hooks.apply(chosen);
	m_applied = true;
	QStringList results = report.lines;
	if (changed) {
		results.prepend(tr("The map changed after the proposal arrived; each edit was checked against it again first."));
	}
	m_resultText->setPlainText(results.join(QLatin1Char('\n')));
	const QSignalBlocker blocker(m_actions);
	for (int row = 0; row < m_actions->topLevelItemCount(); ++row) {
		QTreeWidgetItem* item = m_actions->topLevelItem(row);
		const int index = item->data(EditColumn, Qt::UserRole).toInt();
		item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
		item->setData(EditColumn, Qt::CheckStateRole, QVariant());
		if (report.appliedActions.contains(index)) {
			item->setText(StateColumn, tr("Applied"));
		} else if (const qsizetype failed = report.failedActions.indexOf(index); failed >= 0) {
			item->setText(StateColumn, tr("Failed"));
			item->setText(NotesColumn, report.errors.value(failed));
		} else if (index < chosen.actions.size() && wanted.value(index) && !chosen.actions.at(index).valid()) {
			item->setText(StateColumn, tr("Blocked"));
			item->setText(NotesColumn, chosen.actions.at(index).problems.join(QLatin1Char(' ')));
		} else if (item->text(StateColumn) == tr("Ready")) {
			item->setText(StateColumn, tr("Left out"));
		}
		const QString detail = QStringLiteral("%1: %2").arg(item->text(StateColumn), item->text(NotesColumn));
		item->setToolTip(EditColumn, detail);
		item->setData(EditColumn, Qt::AccessibleDescriptionRole, detail);
	}
	if (report.errors.isEmpty() && nowBlocked == 0) {
		m_status->setText(tr("Applied %n edit(s). Each is its own undo step in Levels; save the map to keep them.", nullptr, report.applied));
	} else {
		m_status->setText(tr("Applied %1 of the checked edits; %n could not be made. See Results under Details.", nullptr, int(report.errors.size()) + nowBlocked)
							  .arg(report.applied));
	}
	updateButtons();
}

bool LevelAiEditDialog::loadProposal(const QString& path, QString* error)
{
	QFile file(path);
	QString message;
	LevelAiEditProposal proposal;
	if (!file.open(QIODevice::ReadOnly)) {
		message = tr("Could not read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
	} else {
		const QString text = QString::fromUtf8(file.readAll());
		QString readError;
		if (!levelAiEditProposalFromAnswer(text, &proposal, &readError)) {
			message = tr("%1 holds no proposal: %2").arg(QDir::toNativeSeparators(path), readError);
		} else {
			m_answerText->setPlainText(text);
		}
	}
	if (!message.isEmpty()) {
		m_status->setText(message);
		if (error) {
			*error = message;
		}
		return false;
	}
	m_contextText->setPlainText(tr("Loaded from %1; nothing was sent.").arg(QDir::toNativeSeparators(path)));
	showProposal(proposal);
	return true;
}

void LevelAiEditDialog::chooseProposalFile()
{
	const QString folder = m_hooks.proposalFolder ? m_hooks.proposalFolder() : QString();
	const QString path = QFileDialog::getOpenFileName(this, tr("Load Map Edit Proposal"), folder, tr("Map edit proposal (*.json)"));
	if (!path.isEmpty()) {
		loadProposal(path);
	}
}

void LevelAiEditDialog::saveProposal()
{
	if (m_proposal.actions.isEmpty()) {
		return;
	}
	const QString folder = m_hooks.proposalFolder ? m_hooks.proposalFolder() : QString();
	const QString path = QFileDialog::getSaveFileName(this, tr("Save Map Edit Proposal"), QDir(folder).filePath(QStringLiteral("map-edits.json")), tr("Map edit proposal (*.json)"));
	if (path.isEmpty()) {
		return;
	}
	const QByteArray bytes = QJsonDocument(levelAiEditProposalJson(checkedProposal())).toJson(QJsonDocument::Indented);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		m_status->setText(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	m_status->setText(tr("Saved the proposal to %1.").arg(QDir::toNativeSeparators(path)));
}

void LevelAiEditDialog::showRequestPreview()
{
	const LevelMapDocument* document = editableDocument();
	if (!document) {
		return;
	}
	const AiTextConnection connection = resolveAiTextConnection(StudioSettings().aiAutomationPreferences());
	const QString instruction = m_instruction->toPlainText().trimmed();
	const QString projectRoot = m_hooks.projectRoot ? m_hooks.projectRoot() : QString();
	const AiChatRequest request = levelAiEditRequest(*document, instruction.isEmpty() ? tr("(your instruction)") : instruction, connection.connectorId, connection.model,
		connection.endpoint, projectRoot, QDir::homePath());
	AiHttpRequest http;
	QString error;
	const bool built = buildAiHttpRequest(request, QString(), &http, &error);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelAiEditRequestPreview"));
	dialog.setWindowTitle(tr("Edit Request"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(760, 560);
	auto* layout = new QVBoxLayout(&dialog);
	auto* explanation = new QLabel(built ? tr("Nothing has been sent. This is the request Ask for Edits would send, without your API key: your instruction and a summary "
											  "of the map, with your home and project folders shortened. %1")
												.arg(aiTextConnectionBlockText(connection))
										 : error);
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto* text = readOnlyText(QStringLiteral("levelAiEditRequestText"), tr("The edit request"));
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

void LevelAiEditDialog::reject()
{
	if (m_busy) {
		cancelRequest();
		return;
	}
	QDialog::reject();
}

void LevelAiEditDialog::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && m_busy) {
		cancelRequest();
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

void LevelAiEditDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	// Back from the map: the selection may have changed.
	if (event->type() == QEvent::ActivationChange && isActiveWindow() && !m_busy) {
		refreshStatus();
	}
}

} // namespace vibestudio
