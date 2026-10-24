#include "app/model_md5_export_dialog.h"
#include "app/locale_number.h"
#include "app/dialog_screen_fit.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMd5ExportDialog)
};
constexpr int kMaxRate = 100000;
bool hasRate(double rate) { return rate > 0 && std::isfinite(rate); }
bool wholeRate(double rate) { return hasRate(rate) && std::abs(rate - std::round(rate)) <= 1e-6 && rate >= 1 && rate <= kMaxRate; }
QString rateText(double rate) { return localeNumber(rate, 6); }
} // namespace

ModelMd5ExportDialog::ModelMd5ExportDialog(const ModelMesh &mesh, int clip, bool allClips, QWidget *parent)
	: QDialog(parent), m_allClips(allClips)
{
	for (const auto &source : mesh.skeleton.clips)
	{
		m_clips.append({source.name, source.framesPerSecond, int(source.frames.size())});
	}
	setObjectName(QStringLiteral("meshMd5ExportDialog"));
	setWindowTitle(allClips ? Text::tr("Export MD5 Animations of All Clips") : Text::tr("Export MD5 Animation"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Choose the skeletal clip and the whole frame rate an md5anim stores, then review the result."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(form);
	m_clip = new QComboBox;
	m_clip->setObjectName(QStringLiteral("meshMd5Clip"));
	m_clip->setAccessibleName(Text::tr("Skeletal clip"));
	m_clip->setAccessibleDescription(Text::tr("The clip written as the md5anim. Doom 3 games use one md5anim per action."));
	m_clip->setToolTip(m_clip->accessibleDescription());
	for (int index = 0; index < m_clips.size(); ++index)
	{
		const auto &entry = m_clips.at(index);
		const QString rate = hasRate(entry.framesPerSecond) ? Text::tr("%1 fps").arg(rateText(entry.framesPerSecond)) : Text::tr("no rate");
		m_clip->addItem(Text::tr("%1 · %n frame(s) · %2", nullptr, entry.frames).arg(entry.name, rate), index);
	}
	m_clip->setCurrentIndex(std::clamp(clip, 0, std::max(0, int(m_clips.size()) - 1)));
	if (!allClips)
	{
		form->addRow(Text::tr("&Clip"), m_clip);
	}
	else
	{
		// Every clip is written; the chooser stays for options() but is not shown.
		m_clip->setParent(this);
		m_clip->hide();
	}
	m_rate = new QSpinBox;
	m_rate->setObjectName(QStringLiteral("meshMd5FrameRate"));
	m_rate->setRange(0, kMaxRate);
	m_rate->setSpecialValueText(Text::tr("Clip's own rate"));
	m_rate->setAccessibleName(Text::tr("MD5 frame rate"));
	m_rate->setAccessibleDescription(
		Text::tr("Frames per second written to the md5anim. Clip's own rate keeps a whole rate; a fractional rate needs a chosen value."));
	m_rate->setToolTip(m_rate->accessibleDescription());
	form->addRow(Text::tr("&Frame rate"), m_rate);
	m_review = new QLabel;
	m_review->setObjectName(QStringLiteral("meshMd5ExportReview"));
	m_review->setTextFormat(Qt::PlainText);
	m_review->setWordWrap(true);
	m_review->setAccessibleName(Text::tr("MD5 animation export review"));
	form->addRow(m_review);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_accept = buttons->button(QDialogButtonBox::Ok);
	m_accept->setObjectName(QStringLiteral("meshMd5ExportAccept"));
	m_accept->setText(Text::tr("Choose Destination…"));
	m_accept->setAccessibleName(m_accept->text());
	m_accept->setAccessibleDescription(Text::tr("Continue to the destination with the reviewed clip and frame rate."));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	// A clip with a fractional rate starts at the nearest whole rate, which
	// the review explains; every clip at once starts at each clip's own rate.
	const auto &chosen = m_clips.value(m_clip->currentIndex());
	if (!allClips && hasRate(chosen.framesPerSecond) && !wholeRate(chosen.framesPerSecond))
	{
		m_rate->setValue(std::clamp(int(std::lround(chosen.framesPerSecond)), 1, kMaxRate));
	}
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	connect(m_rate, &QSpinBox::valueChanged, this, [this] { refresh(); });
	refresh();
	(allClips ? static_cast<QWidget *>(m_rate) : m_clip)->setFocus(Qt::OtherFocusReason);
	fitDialogToScreen(this, QSize(fontMetrics().averageCharWidth() * 72, sizeHint().height()));
}

ModelExportOptions ModelMd5ExportDialog::options() const
{
	ModelExportOptions result;
	result.clip = m_allClips ? -1 : m_clip->currentData().toInt();
	result.md5FrameRate = m_rate->value();
	return result;
}

void ModelMd5ExportDialog::refresh()
{
	const int rate = m_rate->value();
	QString review;
	bool ready = !m_clips.isEmpty();
	if (m_allClips)
	{
		QStringList fractional, unspecified, changed;
		for (const auto &entry : std::as_const(m_clips))
		{
			if (!hasRate(entry.framesPerSecond))
			{
				unspecified << entry.name;
			}
			else if (!wholeRate(entry.framesPerSecond))
			{
				fractional << entry.name;
			}
			if (rate > 0 && hasRate(entry.framesPerSecond) && std::abs(entry.framesPerSecond - rate) > 1e-6)
			{
				changed << entry.name;
			}
		}
		if (rate == 0 && !fractional.isEmpty())
		{
			ready = false;
			review = Text::tr("Clips at a fractional rate, which an md5anim cannot store as a whole number (%n): %1. Choose a frame rate for every clip.",
							  nullptr, int(fractional.size()))
						 .arg(fractional.join(QStringLiteral(", ")));
		}
		else if (rate == 0)
		{
			review = Text::tr("Every clip is written at its own rate, one md5anim per clip (%n file(s)).", nullptr, int(m_clips.size()));
			if (!unspecified.isEmpty())
			{
				review += QLatin1Char(' ') + Text::tr("Clips without a rate are written at 24 frames per second: %1.")
												 .arg(unspecified.join(QStringLiteral(", ")));
			}
		}
		else
		{
			review = Text::tr("Every clip is written at %Ln frame(s) per second.", nullptr, rate);
			if (!changed.isEmpty())
			{
				review += QLatin1Char(' ') + Text::tr("Clips that change playback speed (%n): %1.", nullptr, int(changed.size()))
												 .arg(changed.join(QStringLiteral(", ")));
			}
		}
	}
	else if (ready)
	{
		const auto &entry = m_clips.value(m_clip->currentData().toInt());
		if (rate == 0 && !hasRate(entry.framesPerSecond))
		{
			review = Text::tr("Clip \"%1\" has no frame rate, so it is written at 24 frames per second (%n frame(s)).", nullptr, entry.frames)
						 .arg(entry.name);
		}
		else if (rate == 0 && !wholeRate(entry.framesPerSecond))
		{
			ready = false;
			review = Text::tr("Clip \"%1\" plays at %2 fps, but an md5anim stores a whole number. Choose a frame rate.")
						 .arg(entry.name, rateText(entry.framesPerSecond));
		}
		else if (rate == 0 || (hasRate(entry.framesPerSecond) && std::abs(entry.framesPerSecond - rate) <= 1e-6))
		{
			// The rate is whole here; the frames and the rate each take their
			// own plural form.
			const int written = rate == 0 ? int(std::lround(entry.framesPerSecond)) : rate;
			review = Text::tr("Clip \"%1\" is written with its %2 at %3.", "a clip, its frame count, its frame rate")
						 .arg(entry.name, Text::tr("%Ln frame(s)", nullptr, entry.frames),
							  Text::tr("%Ln frame(s) per second", nullptr, written));
		}
		else if (!hasRate(entry.framesPerSecond))
		{
			review = Text::tr("Clip \"%1\" has no frame rate; it is written at %2 (%3).", "a clip, its frame rate, its frame count")
						 .arg(entry.name, Text::tr("%Ln frame(s) per second", nullptr, rate), Text::tr("%Ln frame(s)", nullptr, entry.frames));
		}
		else
		{
			review = (rate < entry.framesPerSecond
						  ? Text::tr("Clip \"%1\" plays at %2 fps; it is written at %L3, so it plays slower.")
						  : Text::tr("Clip \"%1\" plays at %2 fps; it is written at %L3, so it plays faster."))
						 .arg(entry.name, rateText(entry.framesPerSecond))
						 .arg(rate);
		}
	}
	else
	{
		review = Text::tr("This model has no skeletal clips to write as md5anim.");
	}
	m_review->setText(review);
	m_review->setAccessibleDescription(review);
	m_accept->setEnabled(ready);
}
} // namespace vibestudio
