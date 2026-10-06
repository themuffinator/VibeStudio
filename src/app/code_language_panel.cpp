#include "app/code_language_panel.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {

CodeLanguagePanel::CodeLanguagePanel(QWidget* parent) : QWidget(parent), m_client(new LanguageServerClient(this))
{
	setObjectName(QStringLiteral("codeLanguagePanel"));
	setAccessibleName(tr("Local language server"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_status = new QLabel(tr("Disconnected. Choose a local tool to enable diagnostics, completion and definitions."));
	m_status->setObjectName(QStringLiteral("languageServerStatus"));
	m_status->setAccessibleName(tr("Language server status"));
	m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
	layout->addWidget(m_status);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_program = new QLineEdit;
	m_program->setObjectName(QStringLiteral("languageServerProgram"));
	m_program->setAccessibleName(tr("Language server executable"));
	m_program->setPlaceholderText(tr("Absolute path to clangd or another local server"));
	m_program->setToolTip(tr("Connect starts this trusted local executable in the project folder and shares matching open documents, including unsaved text. The server runs with your account's permissions."));
	m_browse = new QPushButton(tr("Browse…"));
	m_browse->setAccessibleName(tr("Choose language server executable"));
	auto* programRow = new QHBoxLayout;
	programRow->addWidget(m_program, 1); programRow->addWidget(m_browse);
	auto* programLabel = new QLabel(tr("&Executable")); programLabel->setBuddy(m_program);
	form->addRow(programLabel, programRow);
	m_language = new QLineEdit(QStringLiteral("cpp"));
	m_language->setObjectName(QStringLiteral("languageServerLanguage")); m_language->setAccessibleName(tr("Language identifier"));
	auto* languageLabel = new QLabel(tr("&Language")); languageLabel->setBuddy(m_language);
	form->addRow(languageLabel, m_language);
	m_extensions = new QLineEdit(QStringLiteral("c;cc;cpp;cxx;h;hh;hpp;hxx"));
	m_extensions->setObjectName(QStringLiteral("languageServerExtensions")); m_extensions->setAccessibleName(tr("Language server file extensions"));
	m_extensions->setToolTip(tr("Semicolon-separated file extensions. Only matching named documents inside this project are shared."));
	auto* extensionsLabel = new QLabel(tr("File e&xtensions")); extensionsLabel->setBuddy(m_extensions);
	form->addRow(extensionsLabel, m_extensions);
	auto* options = new QWidget;
	auto* optionLayout = new QFormLayout(options); optionLayout->setContentsMargins(0, 0, 0, 0);
	optionLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_arguments = new QPlainTextEdit;
	m_arguments->setObjectName(QStringLiteral("languageServerArguments")); m_arguments->setAccessibleName(tr("Server arguments, one per line"));
	m_arguments->setMaximumHeight(qMax(110, m_arguments->fontMetrics().lineSpacing() * 4));
	m_arguments->setToolTip(tr("One argument per line. Spaces remain inside that argument; no shell quoting or command substitution is performed."));
	auto* argumentLabel = new QLabel(tr("&Arguments")); argumentLabel->setBuddy(m_arguments);
	optionLayout->addRow(argumentLabel, m_arguments);
	options->hide();
	auto* advanced = new QToolButton;
	advanced->setObjectName(QStringLiteral("languageServerDetails"));
	advanced->setText(tr("Arguments and log")); advanced->setAccessibleName(tr("Show server arguments and log")); advanced->setCheckable(true);
	layout->addLayout(form);
	auto* buttons = new QHBoxLayout;
	m_connect = new QPushButton(tr("Connect")); m_connect->setObjectName(QStringLiteral("languageServerConnect")); m_connect->setAccessibleName(tr("Start the configured local language server"));
	m_disconnect = new QPushButton(tr("Disconnect")); m_disconnect->setObjectName(QStringLiteral("languageServerDisconnect")); m_disconnect->setAccessibleName(tr("Stop language server and cancel requests"));
	buttons->addWidget(m_connect); buttons->addWidget(m_disconnect); buttons->addWidget(advanced); buttons->addStretch(); layout->addLayout(buttons);
	m_refreshDiagnostics = new QPushButton(tr("Refresh diagnostics")); m_refreshDiagnostics->setObjectName(QStringLiteral("languageServerRefreshDiagnostics"));
	m_refreshDiagnostics->setAccessibleName(tr("Refresh diagnostics for shared documents"));
	m_refreshDiagnostics->setToolTip(tr("Check the latest open documents again, including after a diagnostic request fails.")); layout->addWidget(m_refreshDiagnostics);
	connect(m_refreshDiagnostics, &QPushButton::clicked, this, [this]() { if (synchronizeNow()) { m_client->refreshDiagnostics(); refresh(); } });
	layout->addWidget(options);
	m_log = new QPlainTextEdit; m_log->setReadOnly(true); m_log->setMaximumBlockCount(128);
	m_log->setObjectName(QStringLiteral("languageServerLog")); m_log->setAccessibleName(tr("Language server log")); m_log->hide();
	layout->addWidget(m_log, 1);
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setTextVisible(false); m_progress->hide(); m_progress->setAccessibleName(tr("Language server operation in progress")); layout->addWidget(m_progress);
	auto* spare = new QWidget; spare->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); layout->addWidget(spare, 1);
	connect(advanced, &QToolButton::toggled, spare, [spare](bool expanded) { spare->setVisible(!expanded); });
	connect(advanced, &QToolButton::toggled, options, &QWidget::setVisible);
	connect(advanced, &QToolButton::toggled, m_log, &QWidget::setVisible);
	connect(m_browse, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr("Choose Language Server"), m_program->text());
		if (!path.isEmpty()) { m_program->setText(QDir::toNativeSeparators(path)); }
	});
	connect(m_connect, &QPushButton::clicked, this, [this]() { connectServer(); });
	connect(m_disconnect, &QPushButton::clicked, this, [this]() { m_syncTimer->stop(); m_client->stop(); });
	m_syncTimer = new QTimer(this); m_syncTimer->setSingleShot(true); m_syncTimer->setInterval(250);
	connect(m_syncTimer, &QTimer::timeout, this, [this]() { synchronizeNow(); });
	m_refreshTimer = new QTimer(this); m_refreshTimer->setInterval(300);
	connect(m_refreshTimer, &QTimer::timeout, this, [this]() { refresh(); });
	m_client->changed = [this]() {
		refresh();
		if (m_client->ready()) { scheduleSync(); }
		if (connectionChanged) { connectionChanged(); }
	};
	refresh();
}
void CodeLanguagePanel::setRootPath(const QString& root)
{
	const QString path = root.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(root).absoluteFilePath());
	if (m_root == path) { return; }
	m_root = path; m_syncError.clear(); m_syncTimer->stop(); m_client->stop(); refresh();
}
void CodeLanguagePanel::setPreferences(const QJsonObject& preferences)
{
	if (m_client->state() != QStringLiteral("stopped") && m_client->state() != QStringLiteral("failed")) { return; }
	m_program->setText(preferences.value(QStringLiteral("program")).toString());
	m_language->setText(preferences.value(QStringLiteral("language")).toString(QStringLiteral("cpp")));
	m_extensions->setText(preferences.value(QStringLiteral("extensions")).toString(QStringLiteral("c;cc;cpp;cxx;h;hh;hpp;hxx")));
	QStringList arguments;
	for (const auto& argument : preferences.value(QStringLiteral("arguments")).toArray()) { if (argument.isString()) { arguments << argument.toString(); } }
	m_arguments->setPlainText(arguments.join(QLatin1Char('\n')));
}
QJsonObject CodeLanguagePanel::preferences() const
{
	return {{QStringLiteral("program"), QDir::fromNativeSeparators(m_program->text().trimmed())}, {QStringLiteral("language"), m_language->text().trimmed()},
		{QStringLiteral("extensions"), m_extensions->text().trimmed()}, {QStringLiteral("arguments"), QJsonArray::fromStringList(m_arguments->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts))}};
}
void CodeLanguagePanel::connectServer()
{
	const auto values = preferences();
	static const QRegularExpression language(QStringLiteral("^[A-Za-z][A-Za-z0-9_+.-]{0,63}$"));
	static const QRegularExpression extensions(QStringLiteral("^[A-Za-z0-9_+.-]+(?:;[A-Za-z0-9_+.-]+)*$"));
	if (!language.match(m_language->text().trimmed()).hasMatch() || !extensions.match(m_extensions->text().trimmed()).hasMatch() || m_extensions->text().size() > 512) {
		m_syncError = tr("Enter a language identifier and semicolon-separated file extensions."); refresh(); return;
	}
	LanguageServerConfig config; config.program = values.value(QStringLiteral("program")).toString(); config.rootPath = m_root;
	for (const auto& argument : values.value(QStringLiteral("arguments")).toArray()) { config.arguments << argument.toString(); }
	m_syncError.clear();
	if (m_client->start(config) && preferencesChanged) { preferencesChanged(values); }
	refresh();
}
void CodeLanguagePanel::scheduleSync()
{
	if (m_client->ready()) { m_syncTimer->start(); }
}
bool CodeLanguagePanel::synchronizeNow()
{
	m_syncTimer->stop();
	if (!m_client->ready() || !snapshots) { return false; }
	QString error;
	QStringList extensions = m_extensions->text().trimmed().toLower().split(QLatin1Char(';'), Qt::SkipEmptyParts);
	for (auto& extension : extensions) { while (extension.startsWith(QLatin1Char('.'))) { extension.remove(0, 1); } }
	const auto documents = snapshots(m_language->text().trimmed(), extensions, &error);
	if (error.isEmpty()) { error = m_client->synchronize(documents); }
	if (!error.isEmpty()) { m_syncError = error; m_client->stop(); refresh(); return false; }
	return true;
}
void CodeLanguagePanel::documentSaved(const QString& path, const QString& text)
{
	if (synchronizeNow()) { m_client->documentSaved(path, text); refresh(); }
}
void CodeLanguagePanel::refresh()
{
	const QString state = m_client->state();
	const bool active = state != QStringLiteral("stopped") && state != QStringLiteral("failed");
	for (QWidget* field : QVector<QWidget*> {m_program, m_language, m_extensions, m_arguments, m_browse}) { field->setEnabled(!active); }
	m_connect->setEnabled(!active && !m_root.isEmpty()); m_disconnect->setEnabled(active && state != QStringLiteral("stopping"));
	m_refreshDiagnostics->setVisible(m_client->ready() && m_client->supportsPullDiagnostics());
	m_progress->setVisible(active && (!m_client->ready() || m_client->pendingRequests() > 0));
	QString text = languageServerStateLabel(state);
	if (!m_client->serverName().isEmpty()) { text += QStringLiteral(" · ") + m_client->serverName(); }
	if (m_client->ready()) {
		text += tr(" · Shared documents: %1").arg(m_client->synchronizedDocuments());
		text += m_client->supportsPullDiagnostics() ? tr(" · Diagnostics requested by the editor") : tr(" · Diagnostics published by the server");
	}
	if (m_root.isEmpty()) { text += tr(" · Open a project to connect."); }
	else { text += QStringLiteral("\n") + QDir::toNativeSeparators(m_root); }
	if (!m_client->error().isEmpty()) { text += QLatin1Char('\n') + m_client->error(); }
	if (!m_syncError.isEmpty()) { text += QLatin1Char('\n') + m_syncError; }
	m_status->setText(text);
	const QString log = m_client->logLines().join(QLatin1Char('\n'));
	if (m_log->toPlainText() != log) { m_log->setPlainText(log); }
	if (active) { if (!m_refreshTimer->isActive()) { m_refreshTimer->start(); } }
	else { m_refreshTimer->stop(); }
}

} // namespace vibestudio
