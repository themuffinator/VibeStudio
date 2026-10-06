#include "app/audio_arrangement_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
AudioArrangementDialog::AudioArrangementDialog(const AudioSession &session, const QStringList &selection,
                                               bool linkedGroups, qint64 cursor, QWidget *parent)
    : QDialog(parent), m_session(session), m_selection(selection)
{
	setObjectName("audioArrangementDialog");
	setWindowTitle(tr("Edit Selected Clips"));
	setAccessibleName(windowTitle());
	resize(560, 520);
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Selection edit controls"));
	auto *body = new QWidget;
	m_form = new QFormLayout(body);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	m_summary = new QLabel;
	m_summary->setObjectName("arrangementSummary");
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setAccessibleName(tr("Selected clip count"));
	m_form->addRow(m_summary);
	m_linked = new QCheckBox(tr("Enabled"));
	m_linked->setAccessibleName(tr("Link grouped clips"));
	m_linked->setObjectName("arrangementLinkedGroups");
	m_linked->setChecked(linkedGroups);
	m_linked->setToolTip(tr("Include every member of a selected group, across all tracks."));
	m_form->addRow(tr("Group links"), m_linked);
	m_operation = new QComboBox;
	m_operation->setObjectName("arrangementOperation");
	m_operation->setAccessibleName(tr("Selection operation"));
	m_operation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_operation->setMinimumContentsLength(12);
	m_operation->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	const QStringList names{tr("Move"),      tr("Duplicate"), tr("Split at cursor"),      tr("Set gain"),
	                        tr("Set fades"), tr("Set mute"),  tr("Group / rename group"), tr("Ungroup"),
	                        tr("Remove")};
	const QStringList ids{"move", "duplicate", "split", "gain", "fades", "mute", "group", "ungroup", "remove"};
	for (int i = 0; i < ids.size(); ++i)
		m_operation->addItem(names[i], ids[i]);
	m_form->addRow(tr("Operation"), m_operation);
	const auto number = [&](const QString &label, const QString &id, double low, double high, int decimals = 0) {
		auto *box = new QDoubleSpinBox;
		box->setObjectName(id);
		box->setAccessibleName(label);
		box->setRange(low, high);
		box->setDecimals(decimals);
		box->setKeyboardTracking(false);
		m_form->addRow(label, box);
		return box;
	};
	m_offset = number(tr("Move by frames"), "arrangementOffset", -double(AudioSessionFrameLimit),
	                  double(AudioSessionFrameLimit));
	m_offset->setToolTip(tr("Positive moves later; negative moves earlier. Relative timing and tracks are preserved. "
	                        "Track automation stays at its authored frames."));
	m_cursor = number(tr("Split frame"), "arrangementCursor", 0, double(AudioSessionFrameLimit));
	m_cursor->setValue(double(cursor));
	m_gain = number(tr("Clip gain (dB)"), "arrangementGain", -96, 24, 2);
	m_fadeIn = number(tr("Fade-in frames"), "arrangementFadeIn", 0, double(AudioSampleLimit));
	m_fadeOut = number(tr("Fade-out frames"), "arrangementFadeOut", 0, double(AudioSampleLimit));
	m_fadeIn->setToolTip(tr("Replace inherited split envelopes with fades over each selected clip's current length."));
	m_fadeOut->setToolTip(m_fadeIn->toolTip());
	m_muted = new QCheckBox(tr("Mute selected clips"));
	m_muted->setObjectName("arrangementMuted");
	m_form->addRow(m_muted);
	m_name = new QLineEdit;
	m_name->setObjectName("arrangementGroupName");
	m_name->setAccessibleName(tr("Group name"));
	m_name->setMaxLength(256);
	m_name->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMMMMMM")) + 24);
	m_form->addRow(tr("Group name"), m_name);
	QString selectionError;
	const auto targets = audioArrangementSelection(session, selection, linkedGroups, &selectionError);
	const QSet<QString> selected(targets.cbegin(), targets.cend());
	QString firstGroup;
	for (const auto &track : session.tracks)
		for (const auto &region : track.regions)
			if (selected.contains(region.id)) {
				if (firstGroup.isEmpty())
					firstGroup = region.groupId;
			}
	for (const auto &group : session.groups)
		if (group.id == firstGroup)
			m_name->setText(group.name);
	m_offset->setValue(0);
	m_error = new QLabel;
	m_error->setObjectName("arrangementError");
	m_error->setTextFormat(Qt::PlainText);
	m_error->setWordWrap(true);
	m_error->setAccessibleName(tr("Selection validation"));
	m_form->addRow(m_error);
	for (int row = 0; row < m_form->rowCount(); ++row)
		if (auto *item = m_form->itemAt(row, QFormLayout::LabelRole))
			if (auto *label = qobject_cast<QLabel *>(item->widget()))
				label->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &AudioArrangementDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_operation, &QComboBox::currentIndexChanged, this, &AudioArrangementDialog::refresh);
	connect(m_linked, &QCheckBox::toggled, this, &AudioArrangementDialog::refresh);
	refresh();
}
AudioArrangementEdit AudioArrangementDialog::edit() const
{
	AudioArrangementEdit result;
	result.operation = m_operation->currentData().toString();
	result.regionIds = m_selection;
	result.linkedGroups = m_linked->isChecked();
	result.offset = qint64(m_offset->value());
	result.position = qint64(m_cursor->value());
	result.gainDb = m_gain->value();
	result.fadeIn = qint64(m_fadeIn->value());
	result.fadeOut = qint64(m_fadeOut->value());
	result.muted = m_muted->isChecked();
	result.name = m_name->text().trimmed();
	return result;
}
void AudioArrangementDialog::refresh()
{
	const auto operation = m_operation->currentData().toString();
	const auto row = [&](QWidget *field, bool visible) {
		field->setVisible(visible);
		if (auto *label = m_form->labelForField(field))
			label->setVisible(visible);
	};
	row(m_offset, operation == "move" || operation == "duplicate");
	row(m_cursor, operation == "split");
	row(m_gain, operation == "gain");
	row(m_fadeIn, operation == "fades");
	row(m_fadeOut, operation == "fades");
	row(m_muted, operation == "mute");
	row(m_name, operation == "group");
	QString error;
	const auto targets = audioArrangementSelection(m_session, m_selection, m_linked->isChecked(), &error);
	m_summary->setText(tr("Selected clips: %1 · Edit targets: %2").arg(m_selection.size()).arg(targets.size()));
	m_summary->setAccessibleDescription(m_summary->text());
	m_error->setText(error);
}
void AudioArrangementDialog::accept()
{
	try {
		const auto result = editAudioArrangement(m_session, edit());
		if (!result.succeeded()) {
			m_error->setText(result.error);
			m_error->setAccessibleDescription(result.error);
			return;
		}
		QDialog::accept();
	} catch (const std::bad_alloc &) {
		m_error->setText(tr("Insufficient memory for the edit. The session is unchanged."));
	}
}
} // namespace vibestudio
