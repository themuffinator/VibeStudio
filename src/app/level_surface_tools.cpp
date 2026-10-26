#include "app/level_surface_tools.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
#include <QComboBox>
#include <QCheckBox>
#include <QAccessible>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace vibestudio {
QStringList LevelSurfaceTools::commandIds()
{
	return {QStringLiteral("map.surfaceShiftLeft"), QStringLiteral("map.surfaceShiftRight"),
		QStringLiteral("map.surfaceShiftDown"), QStringLiteral("map.surfaceShiftUp"),
		QStringLiteral("map.surfaceRotateLeft"), QStringLiteral("map.surfaceRotateRight"),
		QStringLiteral("map.surfaceShrinkU"), QStringLiteral("map.surfaceGrowU"),
		QStringLiteral("map.surfaceShrinkV"), QStringLiteral("map.surfaceGrowV"),
		QStringLiteral("map.surfaceFit"), QStringLiteral("map.surfaceCenter"),
		QStringLiteral("map.surfaceFlipU"), QStringLiteral("map.surfaceFlipV"),
		QStringLiteral("map.surfaceFitU"), QStringLiteral("map.surfaceFitV"),
		QStringLiteral("map.surfaceJustifyLeft"), QStringLiteral("map.surfaceJustifyRight"),
		QStringLiteral("map.surfaceJustifyTop"), QStringLiteral("map.surfaceJustifyBottom"),
		QStringLiteral("map.surfaceNatural"), QStringLiteral("map.surfaceFaceAligned"),
		QStringLiteral("map.surfaceScaleStepLeft"), QStringLiteral("map.surfaceScaleStepRight"),
		QStringLiteral("map.surfaceScaleStepDown"), QStringLiteral("map.surfaceScaleStepUp"),
		QStringLiteral("map.surfaceReset"), QStringLiteral("map.surfaceRadiantReset"),
		QStringLiteral("map.surfaceFitWidth"), QStringLiteral("map.surfaceFitHeight"),
		QStringLiteral("map.surfaceProjectAxial"), QStringLiteral("map.surfaceProjectView"), QStringLiteral("map.surfaceProjectCamera")};
}
LevelSurfaceTools::LevelSurfaceTools(QWidget* parent) : QScrollArea(parent)
{
	setObjectName(QStringLiteral("levelSurfaceTools"));
	setAccessibleName(tr("Surface tools"));
	setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget;
	auto* sections = new QVBoxLayout(body);
	sections->setContentsMargins(0, 0, 0, 0); sections->setSpacing(6);
	// Each section's controls, laid out on a body widget of its own.
	const auto sectionLayout = [] {
		auto* inner = new QVBoxLayout(new QWidget);
		inner->setContentsMargins(0, 0, 0, 0); inner->setSpacing(6);
		return inner;
	};
	QVBoxLayout* const target = sectionLayout();
	QVBoxLayout* const adjust = sectionLayout();
	QVBoxLayout* const clipboard = sectionLayout();
	m_target = new QComboBox;
	m_target->setObjectName(QStringLiteral("surfaceQuickTarget"));
	m_target->setAccessibleName(tr("Surface edit target"));
	m_target->addItems({tr("Selection"), tr("Picked Faces")});
	m_target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_target->setMinimumContentsLength(12);
	m_target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_target->setToolTip(tr("Selection includes selected objects in object mode, or picked brush faces in Face mode, plus independent Select Textured faces and whole patches. Vertex/Edge picks alone have no surface targets. Picked Faces uses independent, component or camera face picks, then the current face in the Inspector."));
	m_target->setAccessibleDescription(m_target->toolTip());
	target->addWidget(m_target);
	m_summary = new QLabel; m_summary->setObjectName(QStringLiteral("surfaceQuickSummary"));
	m_summary->setWordWrap(true); m_summary->setTextFormat(Qt::PlainText);
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_summary->setAccessibleName(tr("Surfaces affected by quick edits")); target->addWidget(m_summary);
	m_status = new QLabel(tr("Select brushes or patches to adjust their texture mapping."));
	m_status->setObjectName(QStringLiteral("surfaceQuickStatus")); m_status->setWordWrap(true);
	m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Surface edit status")); target->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setRange(0, 0);
	m_progress->setAccessibleName(tr("Preparing surface edits")); target->addWidget(m_progress); m_progress->hide();
	m_cancel = new QToolButton; m_cancel->setObjectName(QStringLiteral("surfaceQuickCancel"));
	m_cancel->setText(tr("Cancel")); m_cancel->setAccessibleName(tr("Cancel pending surface edits"));
	m_cancel->setToolTip(tr("Cancel pending adjustments or paste. Completed batches remain in map undo history."));
	m_cancel->setEnabled(false); target->addWidget(m_cancel);

	m_clipboard = new QLabel(tr("No copied surface"));
	m_clipboard->setObjectName(QStringLiteral("surfaceClipboardSummary"));
	m_clipboard->setAccessibleName(tr("Copied surface")); m_clipboard->setWordWrap(true);
	m_clipboard->setTextFormat(Qt::PlainText); m_clipboard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	clipboard->addWidget(m_clipboard);
	auto* clipboardRow = new QHBoxLayout; m_rows << clipboardRow;
	for (const auto& id : {QStringLiteral("map.copySurface"), QStringLiteral("map.pasteSurface")}) {
		auto* button = new QToolButton; button->setObjectName(id);
		button->setText(id == QLatin1String("map.copySurface") ? tr("Copy") : tr("Paste"));
		button->setProperty("fullText", button->text());
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_buttons.insert(id, button); clipboardRow->addWidget(button);
	}
	clipboard->addLayout(clipboardRow);
	m_pasteMode = new QComboBox; m_pasteMode->setObjectName(QStringLiteral("surfacePasteMode"));
	m_pasteMode->setAccessibleName(tr("Surface paste mapping"));
	m_pasteMode->addItems({tr("Matching-format parameters"), tr("World projection"), tr("Seamless wrap"), tr("Radiant values"), tr("Radiant projection"), tr("Face attributes")});
	m_pasteMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_pasteMode->setMinimumContentsLength(12);
	m_pasteMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_pasteMode->setToolTip(tr("Face attributes retains contents, rotates each Valve target's existing axes and copies primitive matrices without rescaling. Parameters reuse native values; Radiant values retains each Valve face's axes. Radiant projection copies classic/Valve parameters, projects primitive matrices and patch UVs, and can leave perpendicular faces edge-on. World projection keeps world UVs in any brush format. Seamless wrap turns mapping around the shared edge; a single wrapped face becomes the next source."));
	clipboard->addWidget(m_pasteMode);
	m_mappingOnly = new QCheckBox; m_mappingOnly->setObjectName(QStringLiteral("surfacePasteMappingOnly"));
	m_mappingOnly->setAccessibleName(tr("Keep target materials and flags"));
	m_mappingOnly->setToolTip(tr("Transfer only mapping. Face attributes copies raw primitive matrices; other primitive transfer modes retain texel density using source and target image dimensions. Radiant values keeps Valve axes and leaves patches unchanged. Radiant projection changes patch UVs while retaining materials."));
	auto* mappingLabel = new QLabel(tr("&Keep materials and flags")); mappingLabel->setWordWrap(true); mappingLabel->setBuddy(m_mappingOnly);
	mappingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	auto* mappingRow = new QHBoxLayout; mappingRow->addWidget(m_mappingOnly); mappingRow->addWidget(mappingLabel, 1); clipboard->addLayout(mappingRow);
	m_allowValve = new QCheckBox; m_allowValve->setObjectName(QStringLiteral("surfacePasteAllowValve"));
	m_allowValve->setAccessibleName(tr("Allow map-wide Valve 220 conversion for surface paste"));
	m_allowValve->setToolTip(tr("If projected or wrapped mapping requires explicit axes, convert all classic faces in this map in the same undo step. Unpasted materials and UVs stay unchanged; locked faces block conversion. Verify support in your game's compiler."));
	m_allowValve->setEnabled(false);
	auto* conversionLabel = new QLabel(tr("&Allow map-wide Valve 220 conversion")); conversionLabel->setWordWrap(true); conversionLabel->setBuddy(m_allowValve);
	conversionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); conversionLabel->setEnabled(false);
	auto* conversionRow = new QHBoxLayout; conversionRow->addWidget(m_allowValve); conversionRow->addWidget(conversionLabel, 1); clipboard->addLayout(conversionRow);
	connect(m_pasteMode, &QComboBox::currentIndexChanged, this, [this, conversionLabel] {
		const bool convertible = m_pasteMode->currentIndex() == 1 || m_pasteMode->currentIndex() == 2;
		m_allowValve->setEnabled(convertible); conversionLabel->setEnabled(convertible);
	});
	const auto step = [&](const QString& title, const QString& id, double value, double maximum, const QString& suffix) {
		auto* label = new QLabel(title); label->setWordWrap(true); adjust->addWidget(label);
		auto* control = new QDoubleSpinBox;
		control->setObjectName(id); control->setAccessibleName(title);
		control->setAccessibleDescription(tr("Amount used by the surface buttons and their keyboard shortcuts."));
		control->setLayoutDirection(Qt::LeftToRight); control->setRange(0.001, maximum);
		control->setDecimals(3); control->setValue(value); control->setSuffix(suffix);
		control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		control->setKeyboardTracking(false); label->setBuddy(control); adjust->addWidget(control); return control;
	};
	// A row of buttons for consecutive commands, from `first`.
	const auto buttonRow = [&](int first, const QStringList& texts) {
		auto* row = new QHBoxLayout;
		m_rows << row;
		const auto ids = commandIds();
		for (int i = 0; i < texts.size(); ++i) {
			auto* button = new QToolButton;
			button->setObjectName(ids[first + i]); button->setText(texts[i]);
			button->setProperty("fullText", button->text());
			button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			m_buttons.insert(ids[first + i], button); row->addWidget(button);
		}
		adjust->addLayout(row);
	};
	const auto pair = [&](int first, const QString& left, const QString& right) { buttonRow(first, {left, right}); };
	m_shift = step(tr("Shift step (texels)"), QStringLiteral("surfaceQuickShift"), 8, 4096, {});
	pair(0, tr("U −"), tr("U +")); pair(2, tr("V −"), tr("V +"));
	m_rotation = step(tr("Rotation step"), QStringLiteral("surfaceQuickRotation"), 15, 180, QStringLiteral("°"));
	{
		// Where Rotate − and + turn: the middle of each face, or the texture
		// origin as Radiant's Surface Inspector does. Editor profiles choose
		// the default (NetRadiant Custom: 45° about the texture origin).
		auto* label = new QLabel(tr("Rotate about")); label->setWordWrap(true); adjust->addWidget(label);
		m_rotationAnchor = new QComboBox;
		m_rotationAnchor->setObjectName(QStringLiteral("surfaceQuickRotationAnchor"));
		m_rotationAnchor->setAccessibleName(tr("Rotate about"));
		m_rotationAnchor->addItems({tr("Face centre"), tr("Texture origin")});
		m_rotationAnchor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		m_rotationAnchor->setMinimumContentsLength(12);
		m_rotationAnchor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		m_rotationAnchor->setToolTip(tr("Face centre keeps the middle of each face's texture in place. Texture origin keeps the offsets, so the texture turns about the point where the world origin projects onto the face (coordinate 0,0 on a patch), as Radiant's Surface Inspector turns it."));
		m_rotationAnchor->setAccessibleDescription(m_rotationAnchor->toolTip());
		label->setBuddy(m_rotationAnchor); adjust->addWidget(m_rotationAnchor);
	}
	pair(4, tr("Rotate −"), tr("Rotate +"));
	m_scale = step(tr("Size step"), QStringLiteral("surfaceQuickScale"), 10, 1000, QStringLiteral(" %"));
	m_scale->setToolTip(tr("Grow multiplies texture size by 1 + percent / 100; shrink divides by that factor. Both keep the face centre fixed."));
	pair(6, tr("U ÷"), tr("U ×")); pair(8, tr("V ÷"), tr("V ×"));
	m_scaleStep = step(tr("Scale step"), QStringLiteral("surfaceQuickScaleStep"), 0.5, 100, {});
	m_scaleStep->setToolTip(tr("Scale U and V − and + subtract this amount from, or add it to, each face's texture scale value about the texture origin, as Radiant's Surface Inspector does; a scale that would reach zero flips sign instead. Patches have no scale value: + multiplies their texture coordinates by the step and − divides them by it."));
	pair(22, tr("Scale U −"), tr("Scale U +")); pair(24, tr("Scale V −"), tr("Scale V +"));
	pair(10, tr("Fit 1 × 1"), tr("Centre"));
	pair(12, tr("Flip U"), tr("Flip V"));
	pair(14, tr("Fit U"), tr("Fit V"));
	// NetRadiant Custom's Fit Width and Fit Height, keeping proportions.
	pair(28, tr("Fit Width"), tr("Fit Height"));
	pair(16, tr("Justify Left"), tr("Justify Right"));
	pair(18, tr("Justify Top"), tr("Justify Bottom"));
	pair(20, tr("Natural"), tr("Align to Face"));
	{
		// NetRadiant Custom's Project Axial, Ortho and Camera.
		auto* label = new QLabel(tr("Project texture")); label->setWordWrap(true);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); adjust->addWidget(label);
		buttonRow(30, {tr("Axial"), tr("2D View"), tr("Camera")});
	}
	pair(26, tr("Reset"), tr("Radiant Reset"));
	// Flags: NetRadiant Custom's Surface Inspector flag boxes and Value field,
	// and TrenchBroom's Face Inspector flags (core/level_face_flags.h).
	QVBoxLayout* const flags = sectionLayout();
	m_flagsSummary = new QLabel;
	m_flagsSummary->setObjectName(QStringLiteral("surfaceFlagsSummary"));
	m_flagsSummary->setWordWrap(true); m_flagsSummary->setTextFormat(Qt::PlainText);
	m_flagsSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_flagsSummary->setAccessibleName(tr("Faces whose flags are shown"));
	flags->addWidget(m_flagsSummary);
	m_flagsBody = new QWidget;
	m_flagsBody->setObjectName(QStringLiteral("surfaceFlagsBody"));
	auto* flagRows = new QVBoxLayout(m_flagsBody);
	flagRows->setContentsMargins(0, 0, 0, 0); flagRows->setSpacing(4);
	const auto heading = [&](const QString& text) {
		auto* label = new QLabel(text); label->setWordWrap(true);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		flagRows->addWidget(label); return label;
	};
	const auto grid = [&](const QString& name) {
		auto* holder = new QWidget; holder->setObjectName(name);
		auto* layout = new QGridLayout(holder);
		layout->setContentsMargins(0, 0, 0, 0); layout->setHorizontalSpacing(8); layout->setVerticalSpacing(2);
		flagRows->addWidget(holder); return layout;
	};
	heading(tr("Surface flags"));
	m_surfaceGrid = grid(QStringLiteral("surfaceFlagsSurface"));
	heading(tr("Content flags"));
	m_contentGrid = grid(QStringLiteral("surfaceFlagsContent"));
	{
		auto* row = new QHBoxLayout;
		// No mnemonic: Alt+V belongs to the View menu.
		auto* label = new QLabel(tr("Value")); label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		m_flagValue = new QLineEdit;
		m_flagValue->setObjectName(QStringLiteral("surfaceFlagValue"));
		m_flagValue->setAccessibleName(tr("Surface value"));
		m_flagValue->setToolTip(tr("The face's surface value, such as a Quake II light's brightness. Press Enter to give it to every targeted face."));
		m_flagValue->setAccessibleDescription(m_flagValue->toolTip());
		m_flagValue->setValidator(new QIntValidator(std::numeric_limits<qint32>::min(), std::numeric_limits<qint32>::max(), m_flagValue));
		// Numbers read left to right in every layout.
		m_flagValue->setLayoutDirection(Qt::LeftToRight);
		m_flagValue->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		label->setBuddy(m_flagValue);
		row->addWidget(label); row->addWidget(m_flagValue, 1); flagRows->addLayout(row);
		connect(m_flagValue, &QLineEdit::editingFinished, this, &LevelSurfaceTools::applyFlagValue);
	}
	m_otherBits = new QLabel; m_otherBits->setObjectName(QStringLiteral("surfaceFlagsOtherBits"));
	m_otherBits->setWordWrap(true); m_otherBits->setTextFormat(Qt::PlainText);
	m_otherBits->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_otherBits->setAccessibleName(tr("Unnamed flag bits")); m_otherBits->hide();
	flagRows->addWidget(m_otherBits);
	m_flagsNote = new QLabel; m_flagsNote->setObjectName(QStringLiteral("surfaceFlagsNote"));
	m_flagsNote->setWordWrap(true); m_flagsNote->setTextFormat(Qt::PlainText);
	m_flagsNote->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_flagsNote->setAccessibleName(tr("About these flags")); m_flagsNote->hide();
	flagRows->addWidget(m_flagsNote);
	{
		auto* row = new QHBoxLayout; m_rows << row;
		auto* clear = new QToolButton; clear->setObjectName(QStringLiteral("map.surfaceClearFlags"));
		clear->setText(tr("Clear Flags")); clear->setProperty("fullText", clear->text());
		clear->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_buttons.insert(clear->objectName(), clear); row->addWidget(clear); flagRows->addLayout(row);
	}
	flags->addWidget(m_flagsBody);
	setFaceFlags(LevelFaceFlagGame::None, {}, false);
	// Bordered like the sidebar's other buttons, so they read as buttons
	// rather than as the flat tool buttons of a tool bar.
	for (QToolButton* button : std::as_const(m_buttons)) { button->setProperty("sidebarButton", true); }
	m_cancel->setProperty("sidebarButton", true);
	const std::pair<QString, QString> titles[] = {{QStringLiteral("surfaces.target"), tr("Target")},
		{QStringLiteral("surfaces.adjust"), tr("Adjust")}, {QStringLiteral("surfaces.clipboard"), tr("Copy and Paste")},
		{QStringLiteral("surfaces.flags"), tr("Flags")}};
	const QVBoxLayout* contents[] = {target, adjust, clipboard, flags};
	for (int index = 0; index < 4; ++index) {
		auto* section = new SidebarSection(titles[index].first, titles[index].second, contents[index]->parentWidget());
		section->setStretch(0); m_sections << section; sections->addWidget(section);
	}
	sections->addStretch(); setWidget(body);
	viewport()->installEventFilter(this); body->installEventFilter(this);
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { if (targetChanged) { targetChanged(); } });
	connect(m_cancel, &QToolButton::clicked, this, [this] { if (cancelRequested) { cancelRequested(); } });
}
bool LevelSurfaceTools::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange) { updateRows(); }
	return QScrollArea::eventFilter(watched, event);
}
void LevelSurfaceTools::updateRows()
{
	// The rows sit in sections, inset by each section's frame and margins.
	const int width = viewport()->width() - 18;
	// What a button needs for its whole label, whatever it shows now.
	const auto chrome = [](const QToolButton* button) { return button->sizeHint().width() - button->fontMetrics().horizontalAdvance(button->text()); };
	const auto whole = [&chrome](const QToolButton* button) {
		return chrome(button) + button->fontMetrics().horizontalAdvance(button->property("fullText").toString());
	};
	for (auto* row : m_rows) {
		QVector<QToolButton*> buttons;
		for (int i = 0; i < row->count(); ++i) {
			if (auto* button = qobject_cast<QToolButton*>(row->itemAt(i)->widget())) { buttons << button; }
		}
		if (buttons.isEmpty()) { continue; }
		int rowWidth = row->spacing() * static_cast<int>(buttons.size() - 1);
		for (const QToolButton* button : std::as_const(buttons)) { rowWidth += whole(button); }
		const auto direction = rowWidth > width ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
		if (row->direction() != direction) { row->setDirection(direction); }
		// A label longer than the panel is wide, at large text or in a long
		// translation, ends in an ellipsis; the tooltip and accessible name
		// keep the command's whole name.
		for (QToolButton* button : std::as_const(buttons)) {
			const QString full = button->property("fullText").toString();
			const QString shown = button->fontMetrics().elidedText(full, Qt::ElideRight, std::max(1, width - chrome(button)));
			if (button->text() != shown) { button->setText(shown); }
		}
	}
	layoutFlagBoxes();
}
void LevelSurfaceTools::rebuildFlagBoxes(LevelFaceFlagGame game)
{
	for (QCheckBox* box : std::as_const(m_surfaceBoxes)) { delete box; }
	for (QCheckBox* box : std::as_const(m_contentBoxes)) { delete box; }
	m_surfaceBoxes.clear(); m_contentBoxes.clear();
	m_flagColumns = 0; m_flagGame = game; m_flagBoxesBuilt = true;
	for (const bool content : {false, true}) {
		const LevelFaceFlagWord word = content ? LevelFaceFlagWord::Content : LevelFaceFlagWord::Surface;
		for (const MaterialFlagDescriptor& flag : levelFaceFlagNames(game, word)) {
			auto* box = new QCheckBox(flag.displayName);
			box->setObjectName((content ? QStringLiteral("contentFlag-") : QStringLiteral("surfaceFlag-")) + flag.id);
			box->setProperty("fullText", flag.displayName);
			box->setProperty("flagBit", static_cast<uint>(flag.bit));
			box->setAccessibleName(content ? tr("Content flag %1").arg(flag.displayName) : tr("Surface flag %1").arg(flag.displayName));
			const QString bit = QStringLiteral("0x%1").arg(flag.bit, 0, 16);
			QString tip = flag.description.isEmpty() ? bit : tr("%1 (%2)").arg(flag.description, bit);
			if (content && flag.bit == kLevelFaceDetailContent) {
				// NRC's Surface Inspector greys out its detail box too.
				tip = tr("Make Detail and Make Structural set this flag on whole brushes.");
			}
			box->setToolTip(tip); box->setAccessibleDescription(tip);
			box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			connect(box, &QCheckBox::clicked, this, [this, box, content] {
				// A partly ticked box ticks on every face; a ticked one clears.
				const bool on = box->checkState() == Qt::Checked;
				box->setTristate(false);
				const quint32 bit = box->property("flagBit").toUInt();
				LevelFaceFlagsEdit edit;
				if (content) { (on ? edit.setContent : edit.clearContent) = bit; }
				else { (on ? edit.setSurface : edit.clearSurface) = bit; }
				if (flagsEdited) { flagsEdited(edit); }
			});
			(content ? m_contentBoxes : m_surfaceBoxes) << box;
		}
	}
	layoutFlagBoxes();
}
void LevelSurfaceTools::layoutFlagBoxes()
{
	if (!m_surfaceGrid || !m_contentGrid) { return; }
	// As many columns as the widest label allows, re-laid out only when that
	// number changes; a label wider than its column ends in an ellipsis, and
	// the accessible name and tooltip keep it whole.
	const int width = std::max(1, viewport()->width() - 18);
	const int spacing = m_surfaceGrid->horizontalSpacing();
	int widest = 1;
	for (const auto* boxes : {&m_surfaceBoxes, &m_contentBoxes}) {
		for (const QCheckBox* box : *boxes) {
			const int chrome = box->sizeHint().width() - box->fontMetrics().horizontalAdvance(box->text());
			widest = std::max(widest, chrome + box->fontMetrics().horizontalAdvance(box->property("fullText").toString()));
		}
	}
	const int columns = std::clamp((width + spacing) / (widest + spacing), 1, 3);
	if (columns != m_flagColumns) {
		m_flagColumns = columns;
		for (auto [grid, boxes] : {std::pair {m_surfaceGrid, &m_surfaceBoxes}, std::pair {m_contentGrid, &m_contentBoxes}}) {
			for (QCheckBox* box : *boxes) { grid->removeWidget(box); }
			for (qsizetype i = 0; i < boxes->size(); ++i) {
				grid->addWidget(boxes->at(i), static_cast<int>(i / columns), static_cast<int>(i % columns));
			}
			for (int column = 0; column < 3; ++column) { grid->setColumnStretch(column, column < columns ? 1 : 0); }
		}
	}
	const int columnWidth = std::max(1, (width - spacing * (columns - 1)) / columns);
	for (const auto* boxes : {&m_surfaceBoxes, &m_contentBoxes}) {
		for (QCheckBox* box : *boxes) {
			const int chrome = box->sizeHint().width() - box->fontMetrics().horizontalAdvance(box->text());
			const QString shown = box->fontMetrics().elidedText(box->property("fullText").toString(), Qt::ElideRight, std::max(1, columnWidth - chrome));
			if (box->text() != shown) { box->setText(shown); }
		}
	}
}
void LevelSurfaceTools::setFaceFlags(LevelFaceFlagGame game, const LevelFaceFlagsSummary& summary, bool editable)
{
	if (!m_flagBoxesBuilt || game != m_flagGame) { rebuildFlagBoxes(game); }
	QString text;
	if (game == LevelFaceFlagGame::None) {
		text = tr("Face flags belong to Quake II and Quake III maps.");
	} else if (summary.faces == 0) {
		text = tr("Select brushes or pick faces to see and change their flags.");
	} else {
		text = game == LevelFaceFlagGame::Quake2 ? tr("Quake II flags of %n brush face(s)", nullptr, summary.faces)
												 : tr("Quake III flags of %n brush face(s)", nullptr, summary.faces);
	}
	if (m_flagsSummary->text() != text) {
		m_flagsSummary->setText(text); m_flagsSummary->setAccessibleDescription(text);
		QAccessibleEvent changed(m_flagsSummary, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
	}
	m_flagsBody->setVisible(game != LevelFaceFlagGame::None);
	const bool canEdit = editable && summary.faces > 0;
	quint32 namedContent = 0, namedSurface = 0;
	for (auto [boxes, all, any, named] : {std::tuple {&m_surfaceBoxes, summary.surfaceAll, summary.surfaceAny, &namedSurface},
			 std::tuple {&m_contentBoxes, summary.contentAll, summary.contentAny, &namedContent}}) {
		for (QCheckBox* box : *boxes) {
			const quint32 bit = box->property("flagBit").toUInt();
			*named |= bit;
			const Qt::CheckState state = (all & bit) != 0 ? Qt::Checked : (any & bit) != 0 ? Qt::PartiallyChecked : Qt::Unchecked;
			box->setTristate(state == Qt::PartiallyChecked);
			if (box->checkState() != state) { box->setCheckState(state); }
			const bool detail = boxes == &m_contentBoxes && bit == kLevelFaceDetailContent;
			box->setEnabled(canEdit && !detail);
		}
	}
	m_flagValue->setEnabled(canEdit);
	m_flagValueShown = summary.faces == 0 || summary.valueMixed ? QString() : QString::number(summary.value);
	if (!m_flagValue->hasFocus() || m_flagValue->text() == m_flagValueShown) { m_flagValue->setText(m_flagValueShown); }
	m_flagValue->setPlaceholderText(summary.valueMixed ? tr("Mixed") : QString());
	// Bits the game leaves unnamed are kept as they are; say so when set.
	const quint32 otherContent = summary.contentAny & ~namedContent, otherSurface = summary.surfaceAny & ~namedSurface;
	m_otherBits->setVisible(otherContent != 0 || otherSurface != 0);
	if (m_otherBits->isVisibleTo(m_flagsBody)) {
		m_otherBits->setText(tr("Unnamed bits kept as they are: content 0x%1, surface 0x%2.")
								 .arg(otherContent, 0, 16).arg(otherSurface, 0, 16));
	}
	// q3map2 reads only the detail bit from a Quake III face; its shaders'
	// surfaceparms give the rest.
	m_flagsNote->setVisible(game == LevelFaceFlagGame::Quake3);
	m_flagsNote->setText(tr("Quake III compilers read only the detail flag from a face; shader surfaceparms set the others in game."));
}
void LevelSurfaceTools::applyFlagValue()
{
	if (!m_flagValue->isEnabled() || !m_flagValue->hasAcceptableInput()) { return; }
	const QString text = m_flagValue->text().trimmed();
	if (text.isEmpty() || text == m_flagValueShown) { return; }
	bool ok = false;
	const qint64 value = text.toLongLong(&ok);
	if (!ok) { return; }
	LevelFaceFlagsEdit edit;
	edit.setValue = true; edit.value = value;
	// Shown now, so focus leaving the field does not apply it twice.
	m_flagValueShown = text;
	if (flagsEdited) { flagsEdited(edit); }
	// A refused value goes back to what the faces hold.
	if (m_flagValue->text().trimmed() != m_flagValueShown) { m_flagValue->setText(m_flagValueShown); }
}
void LevelSurfaceTools::bindCommands(StudioCommandRegistry& commands)
{
	for (auto it = m_buttons.cbegin(); it != m_buttons.cend(); ++it) {
		auto* action = commands.action(it.key());
		bindCommandButton(it.value(), action);
		if (action) { it.value()->setAccessibleName(commandLabelWithoutMnemonic(action->text())); }
	}
}
bool LevelSurfaceTools::singleFace() const { return m_target->currentIndex() == 1; }
void LevelSurfaceTools::setTargetSummary(int faces, LevelSurfaceFace inspected, int patches, bool components)
{
	QString text = components && faces == 0 && (singleFace() || patches == 0) ? tr("No brush-face components selected.")
		: singleFace() ? (faces > 1 ? tr("%n picked face(s)", nullptr, faces)
		: faces == 1 ? tr("Brush %1 · Face %2").arg(inspected.brushId).arg(inspected.faceIndex + 1)
		: tr("Pick faces in the camera, or a face in the Inspector.")) : tr("%n brush face(s) selected", nullptr, faces);
	if (!singleFace() && patches > 0) { text += QLatin1Char('\n') + tr("%n patch(es) selected", nullptr, patches); }
	if (text == m_summary->text()) { return; }
	m_summary->setText(text); m_summary->setAccessibleDescription(text);
	QAccessibleEvent changed(m_summary,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
void LevelSurfaceTools::setStatus(const QString& text, bool busy)
{
	m_status->setText(text); m_progress->setVisible(busy); m_cancel->setEnabled(busy);
	m_status->setAccessibleDescription(text);
	QAccessibleEvent changed(m_status, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
void LevelSurfaceTools::setClipboardSummary(const LevelSurfaceClipboard& clipboard)
{
	const auto format = clipboard.mappingKind() == QLatin1String("matrix") ? tr("Brush matrix")
		: clipboard.mappingKind() == QLatin1String("valve220") ? tr("Valve 220") : tr("Classic");
	const auto text = clipboard.ready() ? tr("%1 · %2 · material, mapping and flags").arg(clipboard.material(), format) : tr("No copied surface");
	if (text == m_clipboard->text()) { return; }
	m_clipboard->setText(text); m_clipboard->setAccessibleDescription(text);
	QAccessibleEvent changed(m_clipboard, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
LevelSurfacePasteOptions LevelSurfaceTools::pasteOptions() const
{
	LevelSurfacePasteOptions result;
	result.mode = m_pasteMode->currentIndex() == 5 ? LevelSurfacePasteMode::Attributes : m_pasteMode->currentIndex() == 4 ? LevelSurfacePasteMode::RadiantProject : m_pasteMode->currentIndex() == 3 ? LevelSurfacePasteMode::RadiantValues : m_pasteMode->currentIndex() == 2 ? LevelSurfacePasteMode::Seamless
		: m_pasteMode->currentIndex() == 1 ? LevelSurfacePasteMode::Project : LevelSurfacePasteMode::Parameters;
	result.mappingOnly = m_mappingOnly->isChecked();
	result.allowValve220 = (result.mode == LevelSurfacePasteMode::Project || result.mode == LevelSurfacePasteMode::Seamless) && m_allowValve->isChecked(); return result;
}
LevelSurfaceRequest LevelSurfaceTools::request(const QString& command) const
{
	const int index = commandIds().indexOf(command);
	LevelSurfaceRequest result;
	if (index < 4) {
		result.operation = LevelSurfaceOperation::Shift;
		result.x = index == 0 ? -m_shift->value() : index == 1 ? m_shift->value() : 0;
		result.y = index == 2 ? -m_shift->value() : index == 3 ? m_shift->value() : 0;
	} else if (index < 6) {
		result.operation = rotatesAboutOrigin() ? LevelSurfaceOperation::RadiantRotate : LevelSurfaceOperation::Rotate;
		result.degrees = m_rotation->value() * (index == 4 ? -1 : 1);
	} else if (index < 10) {
		result.operation = LevelSurfaceOperation::Scale;
		const double factor = 1 + m_scale->value() / 100;
		const double change = index % 2 ? factor : 1 / factor;
		if (index < 8) { result.x = change; } else { result.y = change; }
	} else if (index == 11) {
		result.operation = LevelSurfaceOperation::Align;
	} else if (index == 12 || index == 13) {
		// A flip is a scale of -1 about the middle.
		result.operation = LevelSurfaceOperation::Scale;
		result.x = index == 12 ? -1 : 1;
		result.y = index == 13 ? -1 : 1;
	} else if (index == 14 || index == 15) {
		// A repeat count of zero keeps that axis.
		result.x = index == 14 ? 1 : 0;
		result.y = index == 15 ? 1 : 0;
	} else if (index >= 16 && index <= 19) {
		// Texture V runs down the image, so its top is the minimum.
		result.operation = LevelSurfaceOperation::Align;
		result.alignU = index == 16 ? LevelSurfaceAlignment::Minimum : index == 17 ? LevelSurfaceAlignment::Maximum : LevelSurfaceAlignment::Keep;
		result.alignV = index == 18 ? LevelSurfaceAlignment::Minimum : index == 19 ? LevelSurfaceAlignment::Maximum : LevelSurfaceAlignment::Keep;
	} else if (index == 20 || index == 21) {
		result.operation = index == 20 ? LevelSurfaceOperation::Natural : LevelSurfaceOperation::FaceAligned;
		result.x = result.y = m_defaultScale;
	} else if (index >= 22 && index <= 25) {
		// U −, U +, V −, V +: NetRadiant Custom's TexScaleLeft, Right, Down, Up.
		result.operation = LevelSurfaceOperation::RadiantScale;
		const double amount = m_scaleStep->value() * (index % 2 ? 1 : -1);
		result.x = index < 24 ? amount : 0;
		result.y = index < 24 ? 0 : amount;
	} else if (index == 26 || index == 27) {
		// TrenchBroom's Reset Texture Alignment and Radiant's Reset Texture.
		result.operation = index == 26 ? LevelSurfaceOperation::Reset : LevelSurfaceOperation::RadiantReset;
		result.x = result.y = m_defaultScale;
	} else if (index == 28 || index == 29) {
		// NetRadiant Custom's Fit Width and Fit Height: one repeat across that
		// axis, and the other scaled to match, keeping the proportions.
		result.x = index == 28 ? 1 : 0;
		result.y = index == 29 ? 1 : 0;
		result.keepProportions = true;
	} else if (index >= 30 && index <= 32) {
		// NetRadiant Custom's Project Axial, Ortho and Camera, at the default
		// scale. The shell gives the 2D view's or the camera's direction.
		result.operation = index == 30 ? LevelSurfaceOperation::ProjectAxial : LevelSurfaceOperation::ProjectDirection;
		result.x = result.y = m_defaultScale;
	}
	return result;
}
bool LevelSurfaceTools::rotatesAboutOrigin() const { return m_rotationAnchor->currentIndex() == 1; }
void LevelSurfaceTools::setProfileSteps(double rotationDegrees, bool aboutOrigin)
{
	m_rotation->setValue(rotationDegrees);
	m_rotationAnchor->setCurrentIndex(aboutOrigin ? 1 : 0);
}
} // namespace vibestudio
