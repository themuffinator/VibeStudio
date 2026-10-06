#pragma once

#include "core/language_hover.h"
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QProgressBar;
class QTextBrowser;

namespace vibestudio {

class CodeQuickInfoPanel final : public QWidget {
	Q_OBJECT
public:
	explicit CodeQuickInfoPanel(QWidget* parent = nullptr);
	void begin(const QString& provider, const QString& location);
	void finish(const LanguageHover& report, const QString& provider, const QString& location);
	void invalidate(const QString& reason);
	void showUnavailable(const QString& reason);
	bool busy() const { return m_busy; }
	const LanguageHover& report() const { return m_report; }
	QTextBrowser* view() const { return m_view; }
	std::function<void()> cancelRequested;
	static QString hintText(const LanguageHover& report);
private:
	void setBusy(bool busy);
	QLabel* m_status;
	QPushButton* m_cancel;
	QProgressBar* m_progress;
	QTextBrowser* m_view;
	LanguageHover m_report;
	bool m_busy = false, m_hasInfo = false;
};

} // namespace vibestudio
