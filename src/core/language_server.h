#pragma once

#include "core/language_completion.h"
#include "core/language_hover.h"
#include "core/language_signature.h"
#include "core/language_formatting.h"
#include "core/language_rename.h"
#include "core/language_code_actions.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <functional>

class QProcess;
class QTimer;

namespace vibestudio {

struct LanguageServerConfig {
	QString program;
	QStringList arguments;
	QString rootPath;
	int timeoutMs = 15000;
};

struct LanguageDocument {
	QString filePath;
	QString languageId;
	QString text;
};

struct LanguageLocation {
	QString filePath;
	int line = 0; // Zero-based LSP line and UTF-16 character offsets.
	int character = 0;
	int endLine = 0;
	int endCharacter = 0;
};

struct LanguageDiagnostic {
	LanguageLocation location;
	int severity = 3; // LSP: 1 error, 2 warning, 3 information, 4 hint.
	QString message;
	QString code;
	QString source;
	QJsonObject wire; // Bounded original diagnostic, including provider data.
};

struct LanguageDiagnostics {
	QString filePath;
	int version = -1;
	bool versioned = false;
	bool received = false;
	bool limited = false;
	QVector<LanguageDiagnostic> items = {};
	QString origin = {}, error = {};
	bool pending = false;
	int skipped = 0;
};

inline constexpr int languageReferenceLimit = 10000;
struct LanguageReferences {
	QString filePath;
	int version = 0;
	bool includeDeclaration = true;
	bool limited = false;
	int skipped = 0;
	QString error;
	QVector<LanguageLocation> items;
};

// Original implementation of LSP 3.17 framing and message facts, based on
// Microsoft's CC-BY-4.0 specification, reviewed 2026-10-04. No upstream code
// copied. https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
class LanguageServerFramer {
public:
	bool append(const QByteArray& bytes, QVector<QJsonObject>* messages, QString* error);
	void clear();
	static QByteArray encode(const QJsonObject& message);
private:
	QByteArray m_buffer;
	qsizetype m_length = -1;
};

// Event-driven, local stdio client. All methods/callbacks run on its owning
// thread; no shell invocation, server-requested edits or command execution.
// Start is always explicit. Disconnect retires all document/request state.
class LanguageServerClient final : public QObject {
public:
	explicit LanguageServerClient(QObject* parent = nullptr);
	~LanguageServerClient() override;
	bool start(const LanguageServerConfig& config);
	void stop();
	QString state() const { return m_state; }
	QString error() const { return m_error; }
	QString serverName() const { return m_serverName; }
	QString rootPath() const { return m_config.rootPath; }
	QStringList logLines() const { return m_log; }
	bool ready() const { return m_state == QStringLiteral("ready"); }
	bool supportsDefinition() const { return m_definition; }
	bool supportsCompletion() const { return m_completion; }
	bool supportsCompletionResolve() const { return m_completionResolve; }
	bool supportsReferences() const { return m_references; }
	bool supportsHover() const { return m_hover; }
	bool supportsSignatureHelp() const { return m_signatureHelp; }
	QStringList signatureTriggers(bool retrigger = false) const { return retrigger ? m_signatureRetriggers : m_signatureTriggers; }
	bool supportsFormatting(bool range = false) const { return range ? m_rangeFormatting : m_formatting; }
	bool supportsRename() const { return m_rename; }
	bool supportsPrepareRename() const { return m_prepareRename; }
	bool supportsCodeActions() const { return m_codeActions; }
	bool supportsCodeActionResolve() const { return m_codeActionResolve; }
	bool supportsPullDiagnostics() const { return m_pullDiagnostics; }
	bool supportsSaveNotifications() const { return m_didSave; }
	QStringList completionTriggers() const { return m_completionTriggers; }
	int pendingRequests() const { return m_pending.size(); }
	int synchronizedDocuments() const { return m_documents.size(); }
	// Reconcile all eligible live documents. Removed paths receive didClose.
	// Validation is all-or-nothing; the host must stop on a synchronization error.
	QString synchronize(const QVector<LanguageDocument>& documents);
	int documentVersion(const QString& path) const;
	bool matchesDocument(const QString& path, const QString& text) const;
	LanguageDiagnostics diagnostics(const QString& path) const;
	// Queue diagnostics for a shared document, or all shared documents when empty.
	void refreshDiagnostics(const QString& path = {});
	// Called only after a successful host save and synchronization of its text.
	bool documentSaved(const QString& path, const QString& savedText);
	using DefinitionReply = std::function<void(const QVector<LanguageLocation>&, const QString&)>;
	int definition(const QString& path, int line, int character, DefinitionReply reply);
	using CompletionReply = std::function<void(const LanguageCompletions&)>;
	int completion(const QString& path, int line, int character, int triggerKind, const QString& triggerCharacter, CompletionReply reply);
	using CompletionResolveReply = std::function<void(const LanguageCompletionItem&, const QString&)>;
	int resolveCompletion(const QString& path, int version, const LanguageCompletionItem& item, CompletionResolveReply reply);
	using ReferencesReply = std::function<void(const LanguageReferences&)>;
	int references(const QString& path, int line, int character, bool includeDeclaration, ReferencesReply reply);
	using HoverReply = std::function<void(const LanguageHover&)>;
	int hover(const QString& path, int line, int character, HoverReply reply);
	using SignatureHelpReply = std::function<void(const LanguageSignatureHelp&)>;
	int signatureHelp(const QString& path, int line, int character, int triggerKind, const QString& triggerCharacter,
		bool retrigger, const LanguageSignatureHelp& active, SignatureHelpReply reply);
	using FormattingReply = std::function<void(const LanguageFormatting&)>;
	int formatting(const QString& path, const LanguageFormattingOptions& options, FormattingReply reply);
	using PrepareRenameReply = std::function<void(const LanguageRenamePreparation&)>;
	int prepareRename(const QString& path, int line, int character, PrepareRenameReply reply);
	using RenameReply = std::function<void(const LanguageRename&)>;
	int rename(const QString& path, int line, int character, const QString& newName, RenameReply reply);
	using CodeActionsReply = std::function<void(const LanguageCodeActions&)>;
	int codeActions(const QString& path, int offset, int length, CodeActionsReply reply);
	using CodeActionResolveReply = std::function<void(const LanguageCodeAction&, const QString&)>;
	int resolveCodeAction(const QString& path, int version, const LanguageCodeAction& action, CodeActionResolveReply reply);
	void cancelRequest(int id);
	std::function<void()> changed;
	std::function<void(const LanguageDiagnostics&)> diagnosticsChanged;

private:
	struct Document {
		LanguageDocument source;
		int version = 0;
		LanguageDiagnostics diagnostics, diagnosticCache;
		QString diagnosticResultId;
		int diagnosticRequest = -1, diagnosticRetries = 0;
		bool diagnosticQueued = false;
		qint64 diagnosticRetryAfter = 0;
	};
	using JsonReply = std::function<void(const QJsonValue&, const QString&)>;
	struct Pending { QString method; qint64 deadline = 0; JsonReply reply; };
	void setState(const QString& state);
	void fail(const QString& error);
	void log(const QString& text);
	bool send(const QJsonObject& message);
	void notify(const QString& method, const QJsonObject& params = {});
	int request(const QString& method, const QJsonObject& params, JsonReply reply = {});
	void readOutput();
	void receive(const QJsonObject& message);
	void checkTimeouts();
	void cancelDocumentRequests();
	void clearDocuments();
	void queueDiagnostics(const QString& key, bool resetRetries = true);
	void dispatchDiagnostics();
	bool retryDiagnostics(int id, const QJsonObject& error);
	QProcess* m_process;
	QTimer* m_timer;
	QElapsedTimer m_clock;
	LanguageServerConfig m_config;
	LanguageServerFramer m_framer;
	QHash<QString, Document> m_documents;
	QHash<int, Pending> m_pending;
	QString m_state = QStringLiteral("stopped"), m_error, m_serverName;
	QStringList m_log, m_completionTriggers;
	QStringList m_signatureTriggers, m_signatureRetriggers;
	int m_nextId = 0, m_nextVersion = 0, m_syncKind = 0;
	bool m_definition = false, m_completion = false, m_references = false, m_hover = false;
	bool m_completionResolve = false;
	bool m_signatureHelp = false;
	bool m_formatting = false, m_rangeFormatting = false;
	bool m_rename = false, m_prepareRename = false;
	bool m_codeActions = false, m_codeActionResolve = false;
	bool m_pullDiagnostics = false, m_diagnosticDependencies = false;
	bool m_didSave = false, m_saveText = false;
	QString m_diagnosticIdentifier;
	qint64 m_deadline = 0;
};

LanguageDiagnostics parseLanguageDiagnostics(const QJsonValue& items, const LanguageDocument& document, int version, bool versioned, const QString& origin);

QString languageServerUri(const QString& path);
QString languageServerPath(const QString& uri);
QString languageServerStateLabel(const QString& state);
// Canonical project root; missing named buffers are allowed, links are not.
bool languageProjectSourcePath(const QString& root, const QString& path);

} // namespace vibestudio
