#include "app/audio_placement_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace vibestudio
{
AudioPlacementDialog::AudioPlacementDialog(const LevelSoundTarget& target, const QString& mapName,
                                           const QString& packageName, const LevelSoundRequest& initial,
                                           QWidget* parent)
    : QDialog(parent), m_path(initial.virtualPath)
{
	setObjectName(QStringLiteral("audioPlacementDialog"));
	setWindowTitle(tr("Stage Sound and Place in Level"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Review the game, coordinates and sound entity before staging and placing."));
	auto* layout = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("Sound placement details"));
	auto* content = new QWidget;
	auto* contentLayout = new QVBoxLayout(content);
	scroll->setWidget(content);
	layout->addWidget(scroll);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	const auto label = [&](const QString& text, const QString& name) {
		auto* value = new QLabel(text);
		value->setTextFormat(Qt::PlainText);
		value->setWordWrap(true);
		// Long translated words must wrap within the viewport instead of
		// contributing an unbounded minimum width to the scrolling body.
		value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		value->setAccessibleName(name);
		value->setAccessibleDescription(text);
		return value;
	};
	const auto pathField = [&](const QString& path, const QString& name) {
		auto* value = new QLineEdit(path);
		value->setReadOnly(true);
		value->setAccessibleName(name);
		value->setToolTip(path);
		value->setCursorPosition(0);
		return value;
	};
	form->addRow(tr("Map"), pathField(mapName.isEmpty() ? tr("Untitled map") : mapName, tr("Target map")));
	form->addRow(tr("Package"), pathField(packageName.isEmpty() ? tr("Untitled package") : packageName, tr("Target package")));
	form->addRow(tr("Sound"), pathField(m_path, tr("Staged sound path")));
	m_game = new QComboBox;
	m_game->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_game->setMinimumContentsLength(12);
	m_game->setObjectName(QStringLiteral("audioPlacementGame"));
	m_game->setAccessibleName(tr("Sound game profile"));
	m_game->addItem(tr("Choose the map's game"), QString());
	m_game->addItem(QStringLiteral("Quake II"), QStringLiteral("quake2"));
	m_game->addItem(QStringLiteral("Quake III Arena"), QStringLiteral("quake3"));
	m_game->setCurrentIndex(qMax(0, m_game->findData(initial.game.isEmpty() ? target.game : initial.game)));
	form->addRow(tr("&Game"), m_game);
	const double values[] = {initial.origin.x, initial.origin.y, initial.origin.z};
	const QStringList axes = {tr("&X"), tr("&Y"), tr("&Z")};
	for (int axis = 0; axis < 3; ++axis) {
		auto* position = new QDoubleSpinBox;
		position->setObjectName(QStringLiteral("audioPlacementPosition%1").arg(axis));
		position->setAccessibleName(tr("Sound position %1").arg(QStringLiteral("XYZ").at(axis)));
		position->setRange(-1048576, 1048576);
		position->setDecimals(3);
		position->setSingleStep(8);
		position->setValue(values[axis]);
		m_position[axis] = position;
		form->addRow(axes[axis], position);
	}
	m_mode = new QComboBox;
	m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_mode->setMinimumContentsLength(12);
	m_mode->setObjectName(QStringLiteral("audioPlacementMode"));
	m_mode->setAccessibleName(tr("Sound playback in game"));
	m_mode->addItem(tr("Loop, initially on"), QStringLiteral("loop-on"));
	m_mode->addItem(tr("Loop, initially off"), QStringLiteral("loop-off"));
	m_mode->addItem(tr("Play when triggered"), QStringLiteral("triggered"));
	m_mode->setCurrentIndex(qMax(0, m_mode->findData(initial.mode)));
	form->addRow(tr("&Playback"), m_mode);
	m_targetName = new QLineEdit(initial.targetName);
	m_targetName->setMaxLength(63);
	m_targetName->setObjectName(QStringLiteral("audioPlacementTargetName"));
	m_targetName->setAccessibleName(tr("Sound entity target name"));
	m_targetName->setToolTip(tr("Connect a trigger's target to this name in the level editor. Required for "
	                            "triggered and initially off sounds."));
	form->addRow(tr("&Target name"), m_targetName);
	contentLayout->addLayout(form);
	auto* preview = label({}, tr("Sound placement preview"));
	preview->setObjectName(QStringLiteral("audioPlacementPreview"));
	contentLayout->addWidget(preview);
	contentLayout->addWidget(
	    label(tr("Delivery: mono · 22050 Hz · PCM16 WAV. Save the map and package separately after reviewing "
	             "their pending changes. Each keeps its own undo history."),
	          tr("Sound delivery and save status")));
	contentLayout->addStretch();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* apply = buttons->button(QDialogButtonBox::Ok);
	apply->setText(tr("Stage && &Place"));
	apply->setObjectName(QStringLiteral("audioPlacementApply"));
	apply->setAccessibleName(tr("Stage sound and place the reviewed entity"));
	const auto refresh = [this, target, preview, apply]() {
		m_game->setToolTip(m_game->currentText());
		m_mode->setToolTip(m_mode->currentText());
		const auto plan = planLevelSound(target, request());
		apply->setEnabled(plan.valid());
		preview->setText(plan.valid()
		                     ? tr("%1\nnoise: %2\nspawnflags: %3")
		                           .arg(plan.className, plan.soundReference, plan.properties.at(1).value)
		                     : plan.error);
		preview->setAccessibleDescription(preview->text());
	};
	connect(m_game, &QComboBox::currentIndexChanged, this, refresh);
	connect(m_mode, &QComboBox::currentIndexChanged, this, refresh);
	connect(m_targetName, &QLineEdit::textChanged, this, refresh);
	for (auto* position : m_position) {
		connect(position, &QDoubleSpinBox::valueChanged, this, refresh);
	}
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	refresh();
	resize(600, 650);
}

LevelSoundRequest AudioPlacementDialog::request() const
{
	return {m_game->currentData().toString(),
	        m_path,
	        {m_position[0]->value(), m_position[1]->value(), m_position[2]->value(), true},
	        m_mode->currentData().toString(),
	        m_targetName->text().trimmed()};
}
} // namespace vibestudio
