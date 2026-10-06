#include "app/audio_analysis_dialog.h"
#include "app/studio_icons.h"

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
class AnalysisLabel final : public QLabel
{
public:
	explicit AnalysisLabel(const QString& text, QWidget* parent) : QLabel(text, parent)
	{
		setTextFormat(Qt::PlainText);
		setAccessibleDescription(text);
		setWordWrap(true);
		setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
		setFocusPolicy(Qt::StrongFocus);
	}

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		setMinimumHeight(heightForWidth(width()));
	}
};
} // namespace

AudioChannelMapDialog::AudioChannelMapDialog(int channels, QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("audioChannelMapDialog"));
	setWindowTitle(tr("Loudness Channel Layout"));
	setAccessibleName(tr("Review audio speaker roles"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this);
	auto* scroll = new QScrollArea(this);
	scroll->setAccessibleName(tr("Speaker role controls"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	auto* description = new AnalysisLabel(tr("Assign roles in source channel order. LFE and excluded channels still contribute to true peak, but not integrated loudness."), body);
	description->setAccessibleName(tr("Speaker mapping scope"));
	layout->addWidget(description);
	m_loudness = new QCheckBox(tr("Measure loudness"), body);
	m_loudness->setObjectName(QStringLiteral("audioMeasureLoudness"));
	m_loudness->setAccessibleName(tr("Measure integrated loudness"));
	m_loudness->setChecked(true);
	layout->addWidget(m_loudness);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	layout->addLayout(form);
	auto* preset = new QComboBox(body);
	preset->setObjectName(QStringLiteral("audioChannelLayoutPreset"));
	preset->setAccessibleName(tr("Speaker layout preset"));
	preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	preset->setMinimumContentsLength(14);
	preset->addItem(tr("Custom"), QString());
	const QStringList layouts{QStringLiteral("C"), QStringLiteral("L,R"), QStringLiteral("L,R,C"),
	    QStringLiteral("L,R,Ls,Rs"), QStringLiteral("L,R,C,Ls,Rs"), QStringLiteral("L,R,C,LFE,Ls,Rs"),
	    QStringLiteral("L,R,C,LFE,Cb,Ls,Rs"), QStringLiteral("L,R,C,LFE,Lb,Rb,Ls,Rs")};
	if (channels >= 1 && channels <= layouts.size()) {
		// Speaker IDs are stable technical labels. The separate role controls
		// show their localized meanings without an expanding preset caption.
		preset->addItem(layouts[channels - 1], layouts[channels - 1]);
		preset->setItemData(1, tr("Source channel order: %1").arg(layouts[channels - 1]), Qt::ToolTipRole);
	}
	form->addRow(tr("Layout"), preset);
	for (int channel = 0; channel < channels; ++channel) {
		auto* choice = new QComboBox(body);
		choice->setObjectName(QStringLiteral("audioChannelRole%1").arg(channel + 1));
		choice->setAccessibleName(tr("Channel %1 speaker role").arg(channel + 1));
		choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		choice->setMinimumContentsLength(12);
		choice->addItem(tr("Choose role…"), -1);
		for (int role = 0; role <= static_cast<int>(AudioChannelRole::Unused); ++role) {
			choice->addItem(audioChannelRoleName(static_cast<AudioChannelRole>(role)), role);
		}
		form->addRow(tr("Channel %1").arg(channel + 1), choice);
		m_roles << choice;
	}
	auto* status = new AnalysisLabel({}, body);
	status->setObjectName(QStringLiteral("audioChannelMapStatus"));
	status->setAccessibleName(tr("Channel layout validation"));
	layout->addWidget(status);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Analyze"));
	outer->addWidget(buttons);
	const auto update = [this, preset, status, buttons]() {
		bool valid = true;
		QSet<int> used;
		for (auto* choice : m_roles) {
			choice->setEnabled(m_loudness->isChecked());
			const int role = choice->currentData().toInt();
			valid &= role >= 0;
			if (role != int(AudioChannelRole::LowFrequency) && role != int(AudioChannelRole::Unused)) {
				valid &= !used.contains(role); used.insert(role);
			}
		}
		preset->setEnabled(m_loudness->isChecked());
		const QString message = !m_loudness->isChecked() ? tr("Sample statistics and true peak only.")
		    : valid ? tr("Speaker roles are ready for analysis.") : tr("Choose every role without repeating a speaker position.");
		status->setText(message); status->setAccessibleDescription(message);
		buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_loudness->isChecked() || valid);
	};
	for (auto* choice : m_roles) {
		connect(choice, &QComboBox::currentIndexChanged, this, [preset, update]() {
			const QSignalBlocker blocker(preset); preset->setCurrentIndex(0); update();
		});
	}
	connect(m_loudness, &QCheckBox::toggled, this, update);
	connect(preset, &QComboBox::currentIndexChanged, this, [this, preset, update]() {
		const auto roles = preset->currentData().toString().split(QLatin1Char(','));
		if (roles.size() != m_roles.size()) { return; }
		for (int index = 0; index < roles.size(); ++index) {
			AudioChannelRole role;
			const QSignalBlocker blocker(m_roles[index]);
			if (parseAudioChannelRole(roles[index], &role)) { m_roles[index]->setCurrentIndex(m_roles[index]->findData(int(role))); }
		}
		update();
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	update();
	const QSize available = screen() ? screen()->availableGeometry().size() : QSize(1000, 800);
	resize(std::min(std::max(430, fontMetrics().height() * 28), available.width() - 40),
	       std::min(std::max(420, fontMetrics().height() * (channels * 3 + 12)), available.height() - 80));
}

AudioAnalysisOptions AudioChannelMapDialog::options() const
{
	AudioAnalysisOptions result;
	result.measureLoudness = m_loudness->isChecked();
	if (result.measureLoudness) {
		for (const auto* choice : m_roles) { result.channelMap << static_cast<AudioChannelRole>(choice->currentData().toInt()); }
	}
	return result;
}

AudioAnalysisDialog::AudioAnalysisDialog(AudioAnalysis analysis, const QString& source, QWidget* parent)
    : QDialog(parent), m_analysis(std::move(analysis))
{
	setObjectName(QStringLiteral("audioAnalysisDialog"));
	setWindowTitle(tr("Audio Analysis"));
	setAccessibleName(tr("Audio analysis report"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowModality(Qt::WindowModal);
	if (parent) {
		setLayoutDirection(parent->layoutDirection());
	}
	auto* outer = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("audioAnalysisBody"));
	scroll->setAccessibleName(tr("Analysis report contents"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	layout->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	const auto label = [&](const QString& value, const QString& name) {
		auto* item = new AnalysisLabel(value, body);
		item->setAccessibleName(name);
		layout->addWidget(item);
		return item;
	};
	label(source, tr("Analyzed sound"));
	const qint64 frames = m_analysis.endFrame - m_analysis.firstFrame;
	label(tr("Frames %1–%2 (end exclusive) · %3 s · %4 Hz · Channels: %5")
	          .arg(m_analysis.firstFrame)
	          .arg(m_analysis.endFrame)
	          .arg(locale().toString(double(frames) / std::max(1, m_analysis.sampleRate), 'f', 3))
	          .arg(m_analysis.sampleRate)
	          .arg(m_analysis.channels.size()),
	      tr("Analyzed frame range and format"));
	const auto db = [&](double level) {
		return level > 0 ? locale().toString(20.0 * std::log10(level), 'f', 2) : tr("−∞");
	};
	auto* summary = label(
	    tr("Peak %1 dBFS · RMS %2 dBFS · %n sample(s) above full scale", nullptr, int(m_analysis.samplesAboveFullScale))
	        .arg(db(m_analysis.peak))
	        .arg(db(m_analysis.rms)),
	    tr("Overall sample levels"));
	summary->setObjectName(QStringLiteral("audioAnalysisSummary"));
	label(tr("True peak: %1").arg(m_analysis.truePeak ? tr("%1 dBTP").arg(db(*m_analysis.truePeak)) : tr("Unavailable")), tr("Overall true peak"))
	    ->setObjectName(QStringLiteral("audioAnalysisTruePeak"));
	label(tr("Integrated loudness: %1").arg(m_analysis.integratedLufs ? tr("%1 LUFS").arg(locale().toString(*m_analysis.integratedLufs, 'f', 2))
	    : m_analysis.loudnessStatus == QStringLiteral("below-gate") ? tr("Below gate")
	    : m_analysis.loudnessStatus == QStringLiteral("disabled") ? tr("Not requested") : tr("Unavailable")), tr("Integrated programme loudness"))
	    ->setObjectName(QStringLiteral("audioAnalysisLoudness"));
	if (!m_analysis.truePeakMessage.isEmpty()) { label(m_analysis.truePeakMessage, tr("True-peak availability")); }
	if (!m_analysis.loudnessMessage.isEmpty() && m_analysis.loudnessMessage != m_analysis.truePeakMessage) { label(m_analysis.loudnessMessage, tr("Loudness availability")); }
	auto* table = new QTableWidget(m_analysis.channels.size(), 13, body);
	table->setObjectName(QStringLiteral("audioAnalysisChannels"));
	table->setAccessibleName(tr("Channel measurements"));
	table->setAccessibleDescription(
	    tr("Read-only sample statistics. Frame positions are absolute and channel order follows the source."));
	table->setHorizontalHeaderLabels({tr("Channel"), tr("Peak (dBFS)"), tr("RMS (dBFS)"), tr("DC (%)"),
	                                  tr("Above full scale"), tr("Longest run (frames)"), tr("First over (frame)"),
	                                  tr("Peak frame"), tr("At full scale"), tr("Minimum"), tr("Maximum"), tr("Peak"), tr("True peak (dBTP)")});
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectItems);
	table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	table->setAlternatingRowColors(true);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->setMinimumHeight(fontMetrics().height() * 7);
	const auto frameText = [&](qint64 frame) { return frame >= 0 ? locale().toString(frame) : tr("None"); };
	for (int channel = 0; channel < m_analysis.channels.size(); ++channel) {
		const auto& stats = m_analysis.channels[channel];
		const QStringList values{locale().toString(channel + 1),
		                         db(stats.peak),
		                         db(stats.rms),
		                         locale().toString(stats.dc * 100, 'g', 6),
		                         locale().toString(stats.samplesAboveFullScale),
		                         locale().toString(stats.longestAboveFullScaleRun),
		                         frameText(stats.firstAboveFullScaleFrame),
		                         frameText(stats.peakFrame),
		                         locale().toString(stats.samplesAtFullScale),
		                         locale().toString(stats.minimum, 'g', 9),
		                         locale().toString(stats.maximum, 'g', 9),
		                         locale().toString(stats.peak, 'g', 9),
		                         stats.truePeak ? db(*stats.truePeak) : tr("Unavailable")};
		for (int column = 0; column < values.size(); ++column) {
			// Keep a leading minus beside its value in RTL without changing the
			// localized header or column order. Accessible text omits isolation.
			const bool absentFrame =
			    (column == 6 && stats.firstAboveFullScaleFrame < 0) || (column == 7 && stats.peakFrame < 0) || (column == 12 && !stats.truePeak);
			const QString displayed = absentFrame ? values[column] : QChar(0x2066) + values[column] + QChar(0x2069);
			auto* item = new QTableWidgetItem(displayed);
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
			item->setData(Qt::AccessibleTextRole, values[column]);
			item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			item->setToolTip(table->horizontalHeaderItem(column)->text() + QStringLiteral(": ") + values[column]);
			table->setItem(channel, column, item);
		}
	}
	table->resizeRowsToContents();
	layout->addWidget(table, 1);
	auto* definitions = new QToolButton(body);
	definitions->setObjectName(QStringLiteral("audioAnalysisDefinitions"));
	definitions->setText(tr("Definitions"));
	definitions->setAccessibleName(tr("Show measurement definitions"));
	definitions->setToolTip(definitions->accessibleName());
	definitions->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	definitions->setCheckable(true);
	definitions->setIcon(studioIcon(isRightToLeft() ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right")));
	layout->addWidget(definitions, 0, Qt::AlignLeading);
	auto* details = new QWidget(body);
	auto* detailsLayout = new QVBoxLayout(details);
	detailsLayout->setContentsMargins(0, 0, 0, 0);
	const auto definition = [&](const QString& text, const QString& name) {
		auto* item = new AnalysisLabel(text, details);
		item->setAccessibleName(name);
		detailsLayout->addWidget(item);
	};
	definition(
	    tr("Above full scale counts |sample| > 1; those values saturate in integer delivery. At full scale counts "
	       "exact ±1 values and does not prove that a source was clipped. Runs are measured independently per "
	       "channel."),
	    tr("Full-scale measurement definition"));
	definition(tr("RMS is unweighted and includes DC. Integrated loudness uses K-weighting, complete 400 ms blocks, a -70 LUFS absolute gate and a -10 LU relative gate. Analysis leaves the document unchanged."),
	           tr("Measurement scope"));
	if (m_analysis.truePeakOversampling > 0) {
		definition(tr("True peak uses %1× oversampling of a zero-extended range. Filter and sample-grid limits apply near Nyquist.").arg(m_analysis.truePeakOversampling),
		           tr("True-peak reconstruction"));
	}
	QStringList roles;
	for (int channel = 0; channel < m_analysis.channelMap.size(); ++channel) {
		roles << tr("%1: %2").arg(channel + 1).arg(audioChannelRoleName(m_analysis.channelMap[channel]));
	}
	if (!roles.isEmpty()) { definition(tr("Loudness channels: %1").arg(roles.join(tr("; "))), tr("Measured speaker roles")); }
	layout->addWidget(details);
	details->hide();
	connect(definitions, &QToolButton::toggled, this, [this, definitions, details](bool expanded) {
		details->setVisible(expanded);
		definitions->setIcon(studioIcon(expanded ? QStringLiteral("chevron-down")
		                                      : isRightToLeft() ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right")));
	});
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	outer->addWidget(buttons);
	const QSize available = screen() ? screen()->availableGeometry().size() : QSize(1000, 800);
	resize(std::min(std::max(760, fontMetrics().height() * 44), std::max(400, available.width() - 40)),
	       std::min(std::max(460, fontMetrics().height() * 28), std::max(280, available.height() - 80)));
}

} // namespace vibestudio
