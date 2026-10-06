#pragma once

#include "core/language_signature.h"
#include <QWidget>
#include <functional>

class QComboBox;
class QBoxLayout;
class QLabel;
class QPushButton;
class QProgressBar;
class QTextBrowser;
class QToolButton;

namespace vibestudio {

class CodeSignaturePanel final : public QWidget {
	Q_OBJECT
public:
	explicit CodeSignaturePanel(QWidget* parent = nullptr);
	void begin(const QString& provider);
	void finish(const LanguageSignatureHelp& report, const QString& provider);
	void clear(const QString& reason = {});
	bool busy() const { return m_busy; }
	const LanguageSignatureHelp& report() const { return m_report; }
	std::function<void()> dismissed;
protected:
	void resizeEvent(QResizeEvent* event) override;
private:
	void setBusy(bool busy);
	void showSignature();
	void updateSignatureHeight();
	QBoxLayout* m_controls;
	QLabel* m_status;
	QLabel* m_parameter;
	QComboBox* m_overloads;
	QTextBrowser* m_signature;
	QTextBrowser* m_documentation;
	QToolButton* m_details;
	QPushButton* m_close;
	QProgressBar* m_progress;
	LanguageSignatureHelp m_report;
	bool m_busy = false;
};

} // namespace vibestudio
