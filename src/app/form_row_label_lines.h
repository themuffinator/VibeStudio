#pragma once

#include <QEvent>
#include <QLabel>
#include <QResizeEvent>

namespace vibestudio {

// QFormLayout gives a row label one line's height whatever its wrapped height,
// so a wrapped row label is cut to its first line once large text or a long
// translation makes it wrap. This keeps the label's minimum height at what its
// lines need for the width the form gives it; the form then makes the row that
// tall. ModelEditorDialog's sidebar pages and the modeller's form dialogs use it.
class FormRowLabelLines final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		auto* label = qobject_cast<QLabel*>(watched);
		if (label && label->wordWrap() && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange)) {
			const int width = event->type() == QEvent::Resize ? static_cast<QResizeEvent*>(event)->size().width() : label->width();
			const int height = label->heightForWidth(width);
			if (height > 0 && height != label->minimumHeight()) {
				label->setMinimumHeight(height);
			}
		}
		return false;
	}
};

// Wraps a form's row label and keeps the height of its lines. Calling it again
// for the same label does nothing more.
inline void keepFormRowLabelLines(QLabel* label)
{
	if (!label || label->findChild<QObject*>(QStringLiteral("formRowLabelLines"), Qt::FindDirectChildrenOnly)) {
		return;
	}
	label->setWordWrap(true);
	auto* filter = new FormRowLabelLines(label);
	filter->setObjectName(QStringLiteral("formRowLabelLines"));
	label->installEventFilter(filter);
}

} // namespace vibestudio
