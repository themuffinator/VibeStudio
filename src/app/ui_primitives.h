#pragma once

#include "core/operation_state.h"

#include <QFont>
#include <QFrame>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QListWidget;
class QPushButton;
class QTextEdit;
class QProgressBar;
class QVBoxLayout;
class QWidget;

namespace vibestudio {

class ElidedLabel;

struct UiPrimitiveDescriptor {
	QString id;
	QString title;
	QString description;
	QStringList useCases;
};

struct DetailSection {
	QString id;
	QString title;
	QString summary;
	QString content;
	OperationState state = OperationState::Idle;
};

QVector<UiPrimitiveDescriptor> uiPrimitiveDescriptors();

// A fixed-pitch font for logs, manifests, command lines, and code, sized to
// match the current application font. It prefers well-hinted programming
// fonts and falls back to the platform's fixed font.
QFont studioMonospaceFont();
// Applies that font to a read-only text view, with tab stops four columns
// wide so raw shader, map, and script text keeps its indentation readable.
void applyMonospaceContentFont(QTextEdit* content);

class LoadingPane final : public QFrame {
public:
	explicit LoadingPane(QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setDetail(const QString& detail);
	void setState(OperationState state, const QString& statusText = QString());
	void setProgress(OperationProgress progress);
	void setPlaceholderRows(const QStringList& rows);
	void setReducedMotion(bool reducedMotion);

	QString title() const;
	QString detail() const;
	OperationState state() const;
	OperationProgress progress() const;
	QStringList placeholderRows() const;
	bool reducedMotion() const;

protected:
	// Paints the state's colour along the strip's leading edge (the right
	// edge in a right-to-left layout), which a style sheet cannot mirror.
	void paintEvent(QPaintEvent* event) override;

private:
	void refresh();
	void rebuildPlaceholders();

	QLabel* m_titleLabel = nullptr;
	QLabel* m_stateLabel = nullptr;
	QLabel* m_detailLabel = nullptr;
	QProgressBar* m_progress = nullptr;
	QWidget* m_placeholderHost = nullptr;
	QVBoxLayout* m_placeholderLayout = nullptr;
	QVector<QLabel*> m_placeholderLabels;
	QString m_title;
	QString m_detail;
	QString m_statusText;
	QStringList m_placeholderRows;
	OperationState m_state = OperationState::Idle;
	OperationProgress m_progressValue;
	bool m_reducedMotion = false;
};

class DetailDrawer final : public QFrame {
public:
	explicit DetailDrawer(QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setSubtitle(const QString& subtitle);
	void setSections(const QVector<DetailSection>& sections);
	void showSection(const QString& sectionId);
	void setExpanded(bool expanded);
	// Drops the drawer's own frame and padding when it already sits inside a
	// panel or tab that provides one.
	void setEmbedded(bool embedded);

	QString title() const;
	QString subtitle() const;
	QVector<DetailSection> sections() const;
	QString currentSectionId() const;
	QString currentSectionText() const;
	bool isExpanded() const;

protected:
	void changeEvent(QEvent* event) override;

private:
	void refreshHeader();
	void refreshSections();
	void refreshCurrentSection();
	void copyCurrentSection();
	const DetailSection* findSection(const QString& sectionId) const;

	QLabel* m_titleLabel = nullptr;
	ElidedLabel* m_subtitleLabel = nullptr;
	QPushButton* m_toggleButton = nullptr;
	QPushButton* m_copyButton = nullptr;
	QWidget* m_body = nullptr;
	QLabel* m_emptyLabel = nullptr;
	QListWidget* m_sectionsList = nullptr;
	QTextEdit* m_content = nullptr;
	QString m_title;
	QString m_subtitle;
	QVector<DetailSection> m_sections;
	QString m_currentSectionId;
	QString m_displayedSectionId;
	QString m_displayedContent;
	bool m_expanded = true;
};

} // namespace vibestudio
