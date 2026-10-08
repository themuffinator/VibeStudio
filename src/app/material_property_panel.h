#pragma once

// The properties of the selected graph node, as ordinary controls: numbers,
// choices, switches, image paths and Doom 3 expressions. Each committed
// change becomes one graph edit.

#include "core/material_graph.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QFormLayout;
class QLabel;
class QScrollArea;

namespace vibestudio {

class MaterialPropertyPanel final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialPropertyPanel(QWidget* parent = nullptr);

	// Shows a node's properties; null shows the empty state. A node with the
	// same id and the same set of properties keeps its controls (and the
	// focused one keeps what the user is typing).
	void setNode(const MaterialGraphNode* node, bool readOnly);
	[[nodiscard]] QString nodeId() const { return m_hasNode ? m_node.id : QString(); }
	// Image paths the image fields complete from.
	void setImageChoices(const QStringList& images);
	// Moves keyboard focus to the first editable control.
	void focusFirstField();
	[[nodiscard]] QWidget* editorFor(const QString& propertyId) const;

Q_SIGNALS:
	void propertyEdited(const QString& nodeId, const QString& propertyId, const QString& value);
	void showImageRequested(const QString& reference);

protected:
	void changeEvent(QEvent* event) override;

private:
	void rebuild();
	void refreshValues();
	QWidget* createEditor(const MaterialGraphProperty& property);
	void commit(const QString& propertyId, const QString& value);
	[[nodiscard]] QString signature(const MaterialGraphNode& node) const;

	QLabel* m_title = nullptr;
	QLabel* m_subtitle = nullptr;
	QLabel* m_empty = nullptr;
	QScrollArea* m_scroll = nullptr;
	QWidget* m_formHost = nullptr;
	QFormLayout* m_form = nullptr;
	MaterialGraphNode m_node;
	bool m_hasNode = false;
	bool m_readOnly = false;
	QStringList m_images;
	QHash<QString, QPointer<QWidget>> m_editors;
	QHash<QString, QPointer<QLabel>> m_problems;
};

} // namespace vibestudio
