#include "app/audio_stem_export_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
namespace vibestudio
{
AudioStemExportDialog::AudioStemExportDialog(const AudioSession &session, qint64 first, qint64 end, QWidget *parent)
    : QDialog(parent), m_session(session)
{
	setObjectName("audioStemExportDialog");
	setWindowTitle(tr("Export Stems"));
	setAccessibleName(windowTitle());
	resize(720, 760);
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Stem delivery settings"));
	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	m_strips = new QListWidget;
	m_strips->setObjectName("stemStrips");
	m_strips->setAccessibleName(tr("Tracks and buses to export"));
	m_strips->setWordWrap(true);
	m_strips->setMinimumHeight(130);
	m_strips->setMaximumHeight(230);
	const bool buses =
	    std::any_of(session.tracks.cbegin(), session.tracks.cend(), [](const auto &t) { return t.routing.bus; });
	for (const auto &track : session.tracks) {
		auto *item =
		    new QListWidgetItem(tr("%1 — %2").arg(track.name, track.routing.bus ? tr("Bus") : tr("Track")), m_strips);
		item->setData(Qt::UserRole, track.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState((buses ? track.routing.bus : true) ? Qt::Checked : Qt::Unchecked);
	}
	form->addRow(tr("Stems"), m_strips);
	const auto toggle = [&](const QString &label, const char *id) {
		auto *box = new QCheckBox;
		box->setObjectName(QLatin1String(id));
		box->setAccessibleName(label);
		form->addRow(label, box);
		return box;
	};
	m_master = toggle(tr("Include master mix"), "stemMaster");
	m_master->setChecked(true);
	m_solo = toggle(tr("Respect mixer solos"), "stemSolo");
	m_tap = new QComboBox;
	m_tap->setObjectName("stemTap");
	m_tap->setAccessibleName(tr("Strip signal point"));
	m_tap->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_tap->setMinimumContentsLength(16);
	m_tap->addItem(tr("After fader, pan and inserts"), int(AudioSessionRenderTarget::Tap::PostFader));
	m_tap->addItem(tr("Before fader, pan and inserts"), int(AudioSessionRenderTarget::Tap::PreFader));
	form->addRow(tr("Signal point"), m_tap);
	const auto number = [&](const QString &label, const char *id, qint64 value) {
		auto *box = new QDoubleSpinBox;
		box->setObjectName(QLatin1String(id));
		box->setAccessibleName(label);
		box->setRange(0, double(AudioSessionFrameLimit));
		box->setDecimals(0);
		box->setKeyboardTracking(false);
		box->setValue(double(value));
		form->addRow(label, box);
		return box;
	};
	m_first = number(tr("Start frame"), "stemFirst", first);
	m_end = number(tr("End frame (exclusive)"), "stemEnd", end);
	m_format = new QComboBox;
	m_format->setObjectName("stemFormat");
	m_format->setAccessibleName(tr("WAV precision"));
	for (auto format : {AudioWavFormat::Float32, AudioWavFormat::Pcm24, AudioWavFormat::Pcm16, AudioWavFormat::Pcm32,
	                    AudioWavFormat::Pcm8})
		m_format->addItem(audioWavFormatId(format), int(format));
	form->addRow(tr("WAV precision"), m_format);
	m_dither = toggle(tr("TPDF dither for integer PCM"), "stemDither");
	const auto text = [&](const QString &label, const char *id) {
		auto *edit = new QLineEdit;
		edit->setObjectName(QLatin1String(id));
		edit->setAccessibleName(label);
		form->addRow(label, edit);
		return edit;
	};
	m_seed = text(tr("Dither seed"), "stemSeed");
	m_seed->setText("0");
	m_seed->setMaxLength(20);
	m_seed->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,20}"), m_seed));
	m_prefix = text(tr("Filename prefix"), "stemPrefix");
	m_prefix->setText(session.name);
	m_prefix->setMaxLength(128);
	m_directory = text(tr("Output folder"), "stemDirectory");
	auto *browse = new QPushButton(tr("Choose Folder…"));
	browse->setAccessibleName(tr("Choose stem output folder"));
	form->addRow(browse);
	connect(browse, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getExistingDirectory(this, tr("Stem Output Folder"), m_directory->text());
		if (!path.isEmpty())
			m_directory->setText(path);
	});
	m_overwrite = toggle(tr("Replace existing delivery files"), "stemOverwrite");
	m_overwrite->setToolTip(tr("Every existing file is checked before export and again before replacement."));
	m_plan = new QPlainTextEdit;
	m_plan->setReadOnly(true);
	m_plan->setObjectName("stemPlan");
	m_plan->setAccessibleName(tr("Planned delivery filenames"));
	m_plan->setMinimumHeight(110);
	m_plan->setMaximumHeight(190);
	m_plan->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Delivery files"), m_plan);
	auto *detail = new QLabel(tr("Mutes are preserved. Bus stems include their routed inputs. Overlapping track and "
	                             "bus stems can double-count audio when combined; master processing can also change "
	                             "the sum. Completed files remain available if export is interrupted."));
	detail->setWordWrap(true);
	form->addRow(detail);
	m_status = new QLabel;
	m_status->setWordWrap(true);
	m_status->setObjectName("stemStatus");
	m_status->setAccessibleName(tr("Stem plan status"));
	outer->addWidget(m_status);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Save)->setText(tr("Export"));
	outer->addWidget(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_strips, &QListWidget::itemChanged, this, &AudioStemExportDialog::refresh);
	for (auto *box : {m_master, m_solo, m_overwrite, m_dither})
		connect(box, &QCheckBox::toggled, this, &AudioStemExportDialog::refresh);
	for (auto *box : {m_format, m_tap})
		connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this, &AudioStemExportDialog::refresh);
	for (auto *box : {m_first, m_end})
		connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AudioStemExportDialog::refresh);
	for (auto *edit : {m_prefix, m_directory, m_seed})
		connect(edit, &QLineEdit::textChanged, this, &AudioStemExportDialog::refresh);
	for (auto *label : body->findChildren<QLabel *>()) {
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
	}
	m_status->setTextFormat(Qt::PlainText);
	refresh();
}
AudioStemExportRequest AudioStemExportDialog::request() const
{
	AudioStemExportRequest value;
	for (int i = 0; i < m_strips->count(); ++i)
		if (m_strips->item(i)->checkState() == Qt::Checked)
			value.stripIds << m_strips->item(i)->data(Qt::UserRole).toString();
	value.directory = m_directory->text();
	value.prefix = m_prefix->text();
	value.includeMaster = m_master->isChecked();
	value.respectSolo = m_solo->isChecked();
	value.overwrite = m_overwrite->isChecked();
	value.first = qint64(m_first->value());
	value.end = qint64(m_end->value());
	value.tap = AudioSessionRenderTarget::Tap(m_tap->currentData().toInt());
	value.format = AudioWavFormat(m_format->currentData().toInt());
	value.dither = m_dither->isChecked();
	value.ditherSeed = m_seed->text().toULongLong();
	return value;
}
void AudioStemExportDialog::refresh()
{
	const bool integer = AudioWavFormat(m_format->currentData().toInt()) != AudioWavFormat::Float32;
	if (!integer)
		m_dither->setChecked(false);
	m_dither->setEnabled(integer);
	m_seed->setEnabled(m_dither->isChecked());
	const auto value = request();
	auto planning = value;
	if (planning.directory.trimmed().isEmpty())
		planning.directory = QDir::currentPath();
	const auto plan = planAudioSessionStems(m_session, planning);
	QString error = plan.error;
	bool seedOk = false;
	m_seed->text().toULongLong(&seedOk);
	if (value.dither && !seedOk)
		error = tr("Enter an unsigned 64-bit decimal dither seed.");
	if (error.isEmpty() && (value.directory.trimmed().isEmpty() || !QFileInfo(value.directory).isDir()))
		error = tr("Choose an existing output folder.");
	QStringList files;
	for (const auto &file : plan.files)
		files << QFileInfo(file.path).fileName();
	if (!plan.manifestPath.isEmpty())
		files << QFileInfo(plan.manifestPath).fileName();
	m_plan->setPlainText(files.join('\n'));
	m_status->setText(error.isEmpty()
	                      ? tr("%1 WAV files · %2 frames each.").arg(plan.files.size()).arg(plan.end - plan.first)
	                      : error);
	m_buttons->button(QDialogButtonBox::Save)->setEnabled(error.isEmpty());
}
} // namespace vibestudio
