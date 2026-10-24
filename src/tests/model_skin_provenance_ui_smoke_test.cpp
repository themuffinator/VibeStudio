// The editor's skin-weight provenance surfaces: the Symmetrize joint map
// dialog (Detect Pairs, keyboard focus, accessible names, expanded text),
// the mapped Symmetrize run from the Mesh menu, its cancellation, the status
// line that reports kept, interpolated and mirrored weights, the Add
// Primitive binding dialog on skinned surfaces, and the frame buttons that
// are disabled, with the reason, on the bind frame and skeletal clip frames.

#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_primitive_binding_dialog.h"
#include "app/model_symmetrize_joints_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_skin_provenance_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}
// Longer strings for the dialog, as translations often are.
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "ModelSymmetrizeJointsDialog" && QByteArray(context) != "ModelPrimitiveBindingDialog")
			return {};
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(source, QString("~~ ").repeated(source.size() / 6));
	}
};
void press(QWidget *widget, Qt::Key key)
{
	QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier), up(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &down);
	QApplication::sendEvent(widget, &up);
}
bool inside(QWidget *widget, QWidget *window)
{
	return window->rect().contains(QRect(widget->mapTo(window, QPoint()), widget->size()));
}
// The fixture plus one independent frame after the bind and clip frames.
ModelMesh framesFixture()
{
	auto mesh = skinFixture();
	const int bind = modelBindPoseFrame(mesh);
	auto info = mesh.frames[bind];
	info.name = QStringLiteral("edit");
	mesh.frames.append(info);
	for (auto &surface : mesh.surfaces)
		surface.frames.append(surface.frames[bind]);
	mesh.skeleton.bakedClipForFrame.append(-2);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = test_support::exitCodeWithoutRenderer("model-skin-provenance-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("skin-provenance-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto fixture = skinFixture();
	QString error;

	// The joint map dialog at standard and doubled text, with expanded strings.
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard,
									  scenario ? 200 : 100));
		ModelSymmetrizeJointsDialog dialog(fixture, 0, 0, {}, false);
		dialog.show();
		QCoreApplication::processEvents();
		auto *table = dialog.findChild<QTableWidget *>("symmetrizeJointMap");
		auto *detect = dialog.findChild<QPushButton *>("symmetrizeDetectPairs");
		auto *keep = dialog.findChild<QCheckBox *>("symmetrizeKeepJointNames");
		auto *status = dialog.findChild<QLabel *>("symmetrizeJointsStatus");
		auto *apply = dialog.findChild<QPushButton *>("symmetrizeJointsApply");
		if (!expect(table && detect && keep && status && apply, "joint map controls exist"))
			return EXIT_FAILURE;
		expect(table->rowCount() == 2 && table->item(0, 0)->text() == "arm.L" && table->item(1, 0)->text() == "arm.R" &&
				   table->item(0, 1)->text().isEmpty(),
			   "only the off-centre joints that weight the surface are listed, unmapped");
		expect(dialog.mirrorJoints().isEmpty() && status->text().contains(scenario ? "" : "no mirror joint"),
			   "the status says which joints still need a mirror joint");
		// Screen readers announce each mirror cell by name, with its value.
		const auto cellName = [&](int row)
		{
			auto *view = QAccessible::queryAccessibleInterface(table);
			auto *cells = view ? view->tableInterface() : nullptr;
			auto *cell = cells ? cells->cellAt(row, 1) : nullptr;
			return cell ? cell->text(QAccessible::Name) : QString();
		};
		if (!expect(cellName(0).contains("arm.L") && (scenario || cellName(0).contains("no mirror joint")),
				"an empty mirror cell is announced as having no mirror joint"))
			std::cerr << "  " << cellName(0).toStdString() << '\n';
		detect->click();
		expect(table->item(0, 1)->text() == "arm.R" && table->item(1, 1)->text() == "arm.L" &&
				   dialog.mirrorJoints() == QMap<QString, QString>({{"arm.L", "arm.R"}, {"arm.R", "arm.L"}}),
			   "Detect Pairs fills both directions for review");
		expect(cellName(0).contains("arm.L") && cellName(0).contains("arm.R") && cellName(1).contains("arm.L"),
			   "a filled mirror cell is announced with the joint it mirrors to");
		table->item(1, 1)->setText(QStringLiteral("arm.X"));
		expect(cellName(1).contains("arm.X"), "a typed mirror joint is announced as typed");
		table->item(1, 1)->setText(QStringLiteral("arm.L"));
		expect(scenario || status->text().contains("Every off-centre joint"), "the status confirms a complete map");
		keep->setChecked(true);
		expect(dialog.keepJointNames(), "keeping joint names is an explicit choice");
		for (QWidget *widget : QList<QWidget *>{table, detect, keep, apply})
		{
			expect(QAccessible::queryAccessibleInterface(widget) && !widget->accessibleName().isEmpty() &&
					   !widget->accessibleDescription().isEmpty() && (widget->focusPolicy() & Qt::TabFocus),
				   "controls have accessible names, descriptions and keyboard focus");
			expect(inside(widget, &dialog), "controls fit the dialog at this text size");
		}
		expect(detect->width() >= detect->sizeHint().width() && apply->width() >= apply->minimumSizeHint().width(),
			   "button labels are not clipped");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	// The Add Primitive binding dialog at standard and doubled text.
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard,
									  scenario ? 200 : 100));
		ModelPrimitiveBindingDialog dialog(fixture, 0, QStringLiteral("head"), ModelNewGeometryBinding::Joint, false);
		dialog.show();
		QCoreApplication::processEvents();
		auto *rigid = dialog.findChild<QRadioButton *>("primitiveBindJoint");
		auto *joint = dialog.findChild<QComboBox *>("primitiveBindingJoint");
		auto *nearest = dialog.findChild<QRadioButton *>("primitiveBindNearest");
		auto *separate = dialog.findChild<QRadioButton *>("primitiveNewSurface");
		auto *name = dialog.findChild<QLineEdit *>("primitiveSurfaceName");
		auto *status = dialog.findChild<QLabel *>("primitiveBindingStatus");
		auto *apply = dialog.findChild<QPushButton *>("primitiveBindingApply");
		if (!expect(rigid && joint && nearest && separate && name && status && apply, "binding controls exist"))
			return EXIT_FAILURE;
		expect(rigid->isChecked() && joint->currentText() == "head" && dialog.binding() == ModelNewGeometryBinding::Joint &&
				   dialog.joint() == "head" && dialog.newSurface().isEmpty(),
			   "rigid binding to the selected joint is the default");
		expect(!name->isEnabled() && joint->isEnabled(), "only the chosen option's field is editable");
		nearest->setChecked(true);
		expect(dialog.binding() == ModelNewGeometryBinding::NearestBones && dialog.joint().isEmpty() && !joint->isEnabled(),
			   "nearest-bone binding needs no joint");
		separate->setChecked(true);
		expect(dialog.binding() == ModelNewGeometryBinding::None && !dialog.newSurface().isEmpty() && name->isEnabled() && apply->isEnabled(),
			   "a new unskinned surface gets an unused default name");
		name->setText(QStringLiteral("head"));
		expect(!apply->isEnabled() && (scenario || status->text().contains("already exists")), "a used surface name cannot be applied");
		name->setText(QStringLiteral("helmet"));
		expect(apply->isEnabled() && dialog.newSurface() == "helmet", "a free name can be applied");
		auto *own = dialog.findChild<QCheckBox *>("primitiveOwnSurface");
		if (!expect(own && own->isEnabled() == false && own->isChecked() == false, "an unskinned primitive always gets its own surface"))
			return EXIT_FAILURE;
		rigid->setChecked(true);
		expect(own->isEnabled() && !name->isEnabled() && dialog.newSurface().isEmpty(), "a bound primitive joins the surface unless asked");
		own->setChecked(true);
		expect(dialog.binding() == ModelNewGeometryBinding::Joint && dialog.joint() == "head" && dialog.newSurface() == "helmet" &&
				   name->isEnabled() && apply->isEnabled() && (scenario || status->text().contains("helmet")),
			   "a bound primitive can become a surface of its own");
		separate->setChecked(true);
		expect(!own->isEnabled() && dialog.binding() == ModelNewGeometryBinding::None && dialog.newSurface() == "helmet",
			   "choosing unskinned again keeps the new surface");
		for (QWidget *widget : QList<QWidget *>{rigid, joint, nearest, separate, own, name, apply})
		{
			// The three choices are one radio group: Tab reaches it once and
			// the arrow keys move between them, as Qt's radio groups do.
			const bool radio = qobject_cast<QRadioButton *>(widget);
			if (!expect(QAccessible::queryAccessibleInterface(widget) && !widget->accessibleName().isEmpty() &&
							!widget->accessibleDescription().isEmpty() && (widget->focusPolicy() & (radio ? Qt::ClickFocus : Qt::TabFocus)),
					"binding controls have accessible names, descriptions and keyboard focus"))
				std::cerr << "  " << widget->objectName().toStdString() << '\n';
			expect(inside(widget, &dialog), "binding controls fit the dialog at this text size");
		}
		const QList<QRadioButton *> choices{rigid, nearest, separate};
		expect(std::any_of(choices.cbegin(), choices.cend(), [](QRadioButton *choice) { return (choice->focusPolicy() & Qt::TabFocus) != 0; }),
			   "Tab reaches the binding choices");
		separate->setFocus();
		press(separate, Qt::Key_Up);
		expect(nearest->isChecked() && dialog.binding() == ModelNewGeometryBinding::NearestBones, "the arrow keys move between the binding choices");
		press(nearest, Qt::Key_Down);
		expect(separate->isChecked(), "the arrow keys move back");
		expect(apply->width() >= apply->minimumSizeHint().width(), "the Add Primitive label is not clipped");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));

	auto *editor = new ModelEditorDialog;
	editor->resize(1500, 950);
	editor->show();
	app.processEvents();
	auto *tools = editor->tools();
	auto *status = editor->findChild<QLabel *>("meshStatus");
	if (!expect(tools && status && editor->setMesh(fixture, &error), "the editor opens the skinned fixture"))
		return EXIT_FAILURE;
	const auto opened = skinBytes(editor->document().mesh());

	// Symmetrize from the Mesh menu: the map is confirmed first.
	bool sawDialog = false;
	QTimer::singleShot(0, editor,
		[&]
		{
			auto *dialog = dynamic_cast<ModelSymmetrizeJointsDialog *>(QApplication::activeModalWidget());
			if (!dialog)
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					modal->reject();
				return;
			}
			sawDialog = true;
			dialog->findChild<QPushButton *>("symmetrizeDetectPairs")->click();
			dialog->findChild<QPushButton *>("symmetrizeJointsApply")->click();
		});
	tools->applySelection({0, {}, {0, 1, 2, 3, 4, 5, 6, 7}});
	tools->action(QStringLiteral("meshSymmetrizePositiveX"))->trigger();
	const auto &mesh = editor->document().mesh();
	const int left = findSkinVertex(mesh, 0, {-16, 0, 0}, {16.0f / 32 + .5f, 0});
	expect(sawDialog && left >= 0 && sameSkinWeights(skinWeightsOf(mesh.surfaces[0], left), {{fixtureArmR, 1.0}}),
		   "the confirmed map gives the mirrored half its own side's joints");
	expect(status->text().contains("mirrored"), "the status names the mirrored weights");
	expect(editor->document().canUndo(), "the mapped Symmetrize is one undoable edit");

	// Adjust Last Operation (F9) on another plane: the map confirmed for X
	// does not travel. Across Y every joint sits on the plane, so the replay
	// keeps each side's arm without asking; back across X the map is
	// confirmed again.
	QComboBox *axis = nullptr;
	for (auto *combo : tools->lastOperationPanel()->findChildren<QComboBox *>())
		if (combo->accessibleName() == QStringLiteral("Axis"))
			axis = combo;
	if (expect(axis && axis->isEnabled(), "Adjust Last Operation offers the Symmetrize axis"))
	{
		bool unexpected = false;
		QTimer::singleShot(0, editor,
			[&]
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				{
					unexpected = true;
					modal->reject();
				}
			});
		axis->setCurrentIndex(1);
		app.processEvents();
		const auto &acrossY = editor->document().mesh();
		const int copy = findSkinVertex(acrossY, 0, {16, -8, 0}, {16.0f / 32 + .5f, 1});
		expect(!unexpected && copy >= 0 && sameSkinWeights(skinWeightsOf(acrossY.surfaces[0], copy), {{fixtureArmL, .9}, {fixtureSpine, .1}}),
			   "replaying Symmetrize across Y drops the X joint map without asking");
		bool confirmed = false;
		QTimer::singleShot(0, editor,
			[&]
			{
				auto *dialog = dynamic_cast<ModelSymmetrizeJointsDialog *>(QApplication::activeModalWidget());
				if (!dialog)
				{
					if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
						modal->reject();
					return;
				}
				auto *table = dialog->findChild<QTableWidget *>("symmetrizeJointMap");
				confirmed = table && table->rowCount() == 2 && table->item(0, 1)->text() == "arm.R" && table->item(1, 1)->text() == "arm.L";
				dialog->findChild<QPushButton *>("symmetrizeJointsApply")->click();
			});
		axis->setCurrentIndex(0);
		app.processEvents();
		const auto &acrossX = editor->document().mesh();
		const int mirroredLeft = findSkinVertex(acrossX, 0, {-16, 0, 0}, {16.0f / 32 + .5f, 0});
		expect(confirmed && mirroredLeft >= 0 && sameSkinWeights(skinWeightsOf(acrossX.surfaces[0], mirroredLeft), {{fixtureArmR, 1.0}}),
			   "replaying Symmetrize across X again confirms the remembered map first");
	}
	editor->findChild<QAction *>("undoMesh")->trigger();
	expect(skinBytes(editor->document().mesh()) == opened, "undo restores the source");

	// Cancelling the map makes no change.
	QTimer::singleShot(0, editor,
		[&]
		{
			if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				modal->reject();
		});
	tools->applySelection({0, {}, {0, 1, 2, 3, 4, 5, 6, 7}});
	tools->action(QStringLiteral("meshSymmetrizePositiveX"))->trigger();
	expect(skinBytes(editor->document().mesh()) == opened && status->text().contains("cancelled"),
		   "cancelling the joint map leaves the model unchanged and says so");

	// Surface management reports the weights it kept.
	ModelEdit separate;
	separate.kind = ModelEditKind::SeparateFaces;
	separate.selection = {0, {}, {6, 7}};
	separate.text = QStringLiteral("arm");
	expect(editor->applyEdit(separate, &error) && editor->document().mesh().surfaces.size() == 5 && status->text().contains("kept exactly"),
		   "separating skinned faces reports that their weights were kept");
	editor->findChild<QAction *>("undoMesh")->trigger();

	// Interpolating cuts name the interpolated weights.
	ModelEdit subdivide;
	subdivide.kind = ModelEditKind::Subdivide;
	subdivide.selection = {0, {}, {4, 5}};
	expect(editor->applyEdit(subdivide, &error) && status->text().contains("interpolated"), "subdividing reports interpolated weights");
	editor->findChild<QAction *>("undoMesh")->trigger();

	// Add Primitive on a skinned surface asks how the new vertices follow
	// the skeleton; cancelling makes no change.
	tools->applySelection({0, {}, {}});
	QTimer::singleShot(0, editor,
		[&]
		{
			if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				modal->reject();
		});
	tools->action(QStringLiteral("meshAddCube"))->trigger();
	expect(skinBytes(editor->document().mesh()) == opened && status->text().contains("cancelled"),
		   "cancelling the binding leaves the model unchanged and says so");
	bool sawBinding = false;
	QTimer::singleShot(0, editor,
		[&]
		{
			auto *dialog = dynamic_cast<ModelPrimitiveBindingDialog *>(QApplication::activeModalWidget());
			if (!dialog)
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					modal->reject();
				return;
			}
			sawBinding = true;
			dialog->findChild<QRadioButton *>("primitiveBindJoint")->setChecked(true);
			auto *joint = dialog->findChild<QComboBox *>("primitiveBindingJoint");
			joint->setCurrentIndex(joint->findText("head"));
			dialog->findChild<QPushButton *>("primitiveBindingApply")->click();
		});
	tools->applySelection({0, {}, {}});
	tools->action(QStringLiteral("meshAddCube"))->trigger();
	{
		const auto &added = editor->document().mesh().surfaces[0];
		bool rigid = added.vertexCount > fixture.surfaces[0].vertexCount;
		for (int v = fixture.surfaces[0].vertexCount; rigid && v < added.vertexCount; ++v)
			rigid &= sameSkinWeights(skinWeightsOf(added, v), {{fixtureHead, 1.0}});
		expect(sawBinding && rigid, "the confirmed binding ties every new vertex to the chosen joint");
		expect(status->text().contains("bound"), "the status names the bound vertices");
	}
	// Repeat Last (Shift+R) confirms the binding again, with the last choice.
	{
		bool asked = false;
		QTimer::singleShot(0, editor,
			[&]
			{
				auto *dialog = dynamic_cast<ModelPrimitiveBindingDialog *>(QApplication::activeModalWidget());
				if (!dialog)
				{
					if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
						modal->reject();
					return;
				}
				asked = dialog->findChild<QRadioButton *>("primitiveBindJoint")->isChecked() &&
						dialog->findChild<QComboBox *>("primitiveBindingJoint")->currentText() == "head";
				dialog->findChild<QPushButton *>("primitiveBindingApply")->click();
			});
		const int once = editor->document().mesh().surfaces[0].vertexCount;
		tools->action(QStringLiteral("meshRepeatLast"))->trigger();
		const auto &twice = editor->document().mesh().surfaces[0];
		bool rigid = twice.vertexCount == once + (once - fixture.surfaces[0].vertexCount);
		for (int v = once; rigid && v < twice.vertexCount; ++v)
			rigid &= sameSkinWeights(skinWeightsOf(twice, v), {{fixtureHead, 1.0}});
		expect(asked && rigid, "Repeat Last asks again with the last binding and adds another bound primitive");
		editor->findChild<QAction *>("undoMesh")->trigger();
	}
	editor->findChild<QAction *>("undoMesh")->trigger();
	expect(skinBytes(editor->document().mesh()) == opened, "undo removes the bound primitive");
	// A primitive added as a new unskinned surface repeats into that surface
	// instead of refusing the name it already used.
	{
		QTimer::singleShot(0, editor,
			[&]
			{
				auto *dialog = dynamic_cast<ModelPrimitiveBindingDialog *>(QApplication::activeModalWidget());
				if (!dialog)
				{
					if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
						modal->reject();
					return;
				}
				dialog->findChild<QRadioButton *>("primitiveNewSurface")->setChecked(true);
				dialog->findChild<QPushButton *>("primitiveBindingApply")->click();
			});
		tools->applySelection({0, {}, {}});
		tools->action(QStringLiteral("meshAddCube"))->trigger();
		const int surfaces = editor->document().mesh().surfaces.size();
		const int cube = editor->document().mesh().surfaces.last().vertexCount;
		expect(surfaces == fixture.surfaces.size() + 1 && editor->document().mesh().surfaces.last().skinning.isEmpty(),
			   "the primitive becomes a new unskinned surface");
		bool unexpected = false;
		QTimer::singleShot(0, editor,
			[&]
			{
				if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				{
					unexpected = true;
					modal->reject();
				}
			});
		tools->action(QStringLiteral("meshRepeatLast"))->trigger();
		app.processEvents();
		expect(!unexpected && editor->document().mesh().surfaces.size() == surfaces && editor->document().mesh().surfaces.last().vertexCount == 2 * cube,
			   "Repeat Last adds another primitive to the new unskinned surface");
		editor->findChild<QAction *>("undoMesh")->trigger();
		editor->findChild<QAction *>("undoMesh")->trigger();
		expect(skinBytes(editor->document().mesh()) == opened, "undo removes both primitives");
	}

	// Frame buttons on the bind frame and clip frames are disabled and say why.
	if (!expect(editor->setMesh(framesFixture(), &error), "the editor opens the fixture with an independent frame"))
		return EXIT_FAILURE;
	auto *frame = editor->findChild<QComboBox *>("meshFrame");
	if (!expect(frame && frame->count() == 4, "the frame list shows every frame"))
		return EXIT_FAILURE;
	for (const auto &[index, reason] : {std::pair{0, "bind pose"}, std::pair{1, "skeletal clip wave"}})
	{
		frame->setCurrentIndex(index);
		app.processEvents();
		for (const auto &objectName : {QStringLiteral("duplicateMeshFrame"), QStringLiteral("deleteMeshFrame")})
		{
			auto *button = editor->findChild<QPushButton *>(objectName);
			expect(button && !button->isEnabled() && button->toolTip().contains(reason) && button->accessibleDescription().contains(reason) &&
					   button->toolTip().contains("Edit Skeletal Animation") == (index > 0 || objectName.startsWith("duplicate")),
				   "frame buttons on a retained frame are disabled and name the skeletal alternative");
		}
		auto *copy = editor->findChild<QPushButton *>("copyMeshFramePose");
		expect(copy && !copy->isEnabled() && copy->accessibleDescription().contains(reason), "Copy Full Pose into a retained frame is disabled");
	}
	frame->setCurrentIndex(1);
	app.processEvents();
	auto *inbetween = editor->findChild<QPushButton *>("insertMeshInbetweens");
	expect(inbetween && !inbetween->isEnabled() && inbetween->toolTip().contains("interpolate"),
		   "in-betweens between two clip frames are disabled");
	frame->setCurrentIndex(3);
	app.processEvents();
	for (const auto &objectName : {QStringLiteral("duplicateMeshFrame"), QStringLiteral("deleteMeshFrame")})
	{
		auto *button = editor->findChild<QPushButton *>(objectName);
		expect(button && button->isEnabled() && !button->toolTip().contains("skeleton"), "frame buttons on an independent frame are enabled");
	}
	auto *copy = editor->findChild<QPushButton *>("copyMeshFramePose");
	expect(copy && copy->toolTip().contains("Replace the displayed pose"), "Copy Full Pose keeps its own description on an independent frame");
	editor->findChild<QPushButton *>("duplicateMeshFrame")->click();
	expect(editor->document().mesh().frames.size() == 5 &&
			   editor->document().mesh().skeleton.bakedClipForFrame == QVector<int>({-1, 0, 0, -2, -2}),
		   "duplicating the independent frame keeps the clip mapping");
	// Once every surface is unbound, nothing follows the skeleton and the
	// frame buttons work on every frame.
	{
		auto unbound = framesFixture();
		for (auto &surface : unbound.surfaces)
			surface.skinning = {};
		updateEditableModelMetadata(&unbound);
		if (!expect(editor->setMesh(unbound, &error), "the editor opens the unbound skeletal fixture"))
			return EXIT_FAILURE;
		frame->setCurrentIndex(1);
		app.processEvents();
		for (const auto &objectName :
			{QStringLiteral("duplicateMeshFrame"), QStringLiteral("deleteMeshFrame"), QStringLiteral("copyMeshFramePose"), QStringLiteral("insertMeshInbetweens")})
		{
			auto *button = editor->findChild<QPushButton *>(objectName);
			if (!expect(button && button->isEnabled() && !button->toolTip().contains("skeleton"), "frame buttons on an unbound model's clip frame are enabled"))
				std::cerr << "  " << objectName.toStdString() << '\n';
		}
		editor->findChild<QPushButton *>("duplicateMeshFrame")->click();
		expect(editor->document().mesh().skeleton.bakedClipForFrame == QVector<int>({-1, 0, -2, 0, -2}),
			   "duplicating a clip frame of an unbound model keeps the other frames' clip mapping");
	}
	delete editor;
	std::cout << checks << " skin provenance UI checks, " << failures << " failures\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
