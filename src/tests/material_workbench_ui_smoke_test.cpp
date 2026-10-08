// The Materials workbench as a widget: scanning a package, choosing
// materials, the live preview, node and property edits that rewrite the
// text, text edits that rebuild the graph, undo, keyboard navigation and
// screen-reader children in the graph, saving loose scripts, and staging
// scripts, Doom lumps and Quake II WAL headers through the host hooks.
// Every package is generated here; no game data.

#include "app/code_editor.h"
#include "app/material_graph_view.h"
#include "app/material_library_model.h"
#include "app/material_preview_view.h"
#include "app/material_property_panel.h"
#include "app/material_workbench.h"
#include "app/studio_theme.h"
#include "core/package_archive.h"

#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTreeWidget>
#include <QtEndian>

#include <algorithm>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 20000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition()) {
		if (timer.elapsed() > timeoutMs) {
			return false;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return true;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray tga(QRgb a, QRgb b, int size = 32)
{
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(size), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const QRgb pixel = ((x / 8 + y / 8) % 2) == 0 ? a : b;
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append(static_cast<char>(qAlpha(pixel)));
		}
	}
	return bytes;
}

struct Lump {
	QByteArray name;
	QByteArray data;
};

QByteArray doomWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(16, '\0');
		qToLittleEndian<qint32>(lump.data.isEmpty() ? 0 : offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		for (int i = 0; i < 8 && i < lump.name.size(); ++i) {
			entry[8 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += static_cast<int>(lump.data.size());
	}
	QByteArray header("PWAD", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray doomFixture()
{
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>((index * 7) % 256));
		palette.append(static_cast<char>(255 - index));
	}
	QByteArray colormap;
	for (int level = 0; level < 34; ++level) {
		for (int index = 0; index < 256; ++index) {
			colormap.append(static_cast<char>(level < 32 ? std::max(0, index - level * 4) : index));
		}
	}
	const auto flat = [](int index) { return QByteArray(64 * 64, static_cast<char>(index)); };
	return doomWad({{"PLAYPAL", palette.repeated(14)}, {"COLORMAP", colormap}, {"F_START", {}}, {"NUKAGE1", flat(100)}, {"NUKAGE2", flat(140)},
		{"NUKAGE3", flat(180)}, {"F_END", {}}});
}

QByteArray quake2Wal(const QByteArray& name, quint32 flags, qint32 value)
{
	QByteArray bytes(100, '\0');
	bytes.replace(0, name.size(), name);
	qToLittleEndian<quint32>(32, bytes.data() + 32);
	qToLittleEndian<quint32>(32, bytes.data() + 36);
	quint32 offset = 100;
	for (int level = 0; level < 4; ++level) {
		qToLittleEndian<quint32>(offset, bytes.data() + 40 + 4 * level);
		offset += static_cast<quint32>((32 >> level) * (32 >> level));
	}
	qToLittleEndian<quint32>(flags, bytes.data() + 88);
	qToLittleEndian<qint32>(value, bytes.data() + 96);
	for (int level = 0; level < 4; ++level) {
		const int side = 32 >> level;
		bytes.append(QByteArray(side * side, static_cast<char>(40 + level)));
	}
	return bytes;
}

const char* kShaders = R"(// Workbench fixture
textures/test/glow
{
	surfaceparm nolightmap
	{
		map textures/test/glow.tga
		tcMod scroll 0.5 0
		rgbGen wave sin 0.5 0.5 0 1
	}
}

textures/test/anim
{
	{
		animMap 2 textures/test/a.tga textures/test/b.tga
	}
}

textures/test/bad
{
	{
		map textures/test/glow.tga
		frobnicate 1
	}
}
)";

const char* kMaterials = R"(table pulseTable { { 0, 1 } }

textures/test/panel
{
	diffusemap textures/test/panel_d
	specularmap textures/test/panel_s
}

textures/test/pulse
{
	{
		blend add
		map textures/test/panel_d
		rgb pulseTable[time]
	}
}
)";

std::shared_ptr<PackageArchive> openPackage(const QString& path)
{
	auto archive = std::make_shared<PackageArchive>();
	QString error;
	expect(archive->load(path, &error), "the fixture package opens", error);
	return archive;
}

bool hasNode(MaterialGraphView* graph, const QString& id)
{
	return graph->graph().indexOf(id) >= 0;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// MATERIAL_TEST_LOOK=high-contrast-light, -dark, light or dark;
	// MATERIAL_TEST_SCALE=200; MATERIAL_TEST_RTL=1: the same checks in another
	// look, for snapshots of the page at the sizes and directions it supports.
	const QString look = qEnvironmentVariable("MATERIAL_TEST_LOOK");
	const StudioTheme theme = look == QStringLiteral("high-contrast-light") ? StudioTheme::HighContrastLight
		: look == QStringLiteral("high-contrast-dark")						  ? StudioTheme::HighContrastDark
		: look == QStringLiteral("light")									  ? StudioTheme::Light
																			  : StudioTheme::Dark;
	const int scale = qEnvironmentVariableIsSet("MATERIAL_TEST_SCALE") ? qEnvironmentVariableIntValue("MATERIAL_TEST_SCALE") : 100;
	applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, std::clamp(scale, 100, 200)));
	if (qEnvironmentVariableIntValue("MATERIAL_TEST_RTL") == 1) {
		QApplication::setLayoutDirection(Qt::RightToLeft);
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	const QDir root(temp.path());
	const QString q3 = root.filePath(QStringLiteral("q3"));
	writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(qRgb(255, 200, 80), qRgb(30, 20, 10)));
	writeFile(q3 + QStringLiteral("/textures/test/a.tga"), tga(qRgb(255, 0, 0), qRgb(255, 0, 0)));
	writeFile(q3 + QStringLiteral("/textures/test/b.tga"), tga(qRgb(0, 0, 255), qRgb(0, 0, 255)));
	writeFile(q3 + QStringLiteral("/scripts/test.shader"), kShaders);
	const QString d3 = root.filePath(QStringLiteral("d3"));
	writeFile(d3 + QStringLiteral("/textures/test/panel_d.tga"), tga(qRgb(160, 160, 170), qRgb(80, 80, 90)));
	writeFile(d3 + QStringLiteral("/textures/test/panel_s.tga"), tga(qRgb(90, 90, 90), qRgb(20, 20, 20)));
	writeFile(d3 + QStringLiteral("/materials/test.mtr"), kMaterials);
	const QString wad = root.filePath(QStringLiteral("test.wad"));
	writeFile(wad, doomFixture());
	const QString q2 = root.filePath(QStringLiteral("q2"));
	writeFile(q2 + QStringLiteral("/textures/e1u1/water1.wal"), quake2Wal("e1u1/water1", 0x8, 0));

	MaterialWorkbench workbench;
	QStringList staged;
	QHash<QString, QByteArray> stagedBytes;
	QStringList shownTextures;
	MaterialWorkbenchHost host;
	host.stageFile = [&](const QByteArray& bytes, const QString& path, QString*) {
		staged << path;
		stagedBytes.insert(path, bytes);
		return true;
	};
	host.stageLump = [&](const QByteArray& bytes, const QString& name, QString*) {
		staged << QStringLiteral("lump:") + name;
		stagedBytes.insert(QStringLiteral("lump:") + name, bytes);
		return true;
	};
	host.showTexture = [&](const QString& reference) { shownTextures << reference; };
	workbench.setHost(host);
	workbench.resize(1400, 900);
	workbench.show();
	const auto idle = [&]() {
		const bool done = waitFor([&]() { return !workbench.isBusy(); });
		if (!done) {
			std::cerr << "  still busy: " << workbench.busyReason().toStdString() << std::endl;
		}
		return done;
	};
	const auto rendered = [&]() {
		// A playing preview always has a frame coming; a drawn one is enough.
		return waitFor([&]() {
			return !workbench.isBusy() && !workbench.preview()->lastImage().isNull()
				&& (workbench.preview()->isPlaying() || !workbench.preview()->renderPending());
		});
	};

	// Scanning and choosing.
	auto archive = openPackage(q3);
	workbench.setPackage(archive, q3, QStringLiteral("q3-1"));
	expect(workbench.hasContent(), "a scanning workbench has content to show");
	expect(idle(), "the Quake III package scans");
	expect(workbench.libraryModel()->rowCount() >= 3, "the library lists the shaders", QString::number(workbench.libraryModel()->rowCount()));
	expect(workbench.selectMaterial(QStringLiteral("textures/test/glow")), "a shader can be chosen by name");
	expect(rendered(), "the preview draws the shader");
	expect(workbench.currentTextKind() == MaterialTextKind::Quake3Shader && workbench.currentText().contains(QStringLiteral("textures/test/glow")),
		"the text editor shows the shader script");
	expect(!workbench.preview()->lastResult().fallback, "a valid shader is drawn, not the default shader");
	MaterialGraphView* graph = workbench.graphView();
	expect(hasNode(graph, QStringLiteral("stage/0/tcmod/0")) && hasNode(graph, QStringLiteral("material")), "the graph shows the stage's nodes");

	// A graph edit rewrites the text as one undo step.
	MaterialGraphEdit frequency;
	frequency.kind = MaterialGraphEditKind::SetProperty;
	frequency.node = QStringLiteral("stage/0/rgbgen");
	frequency.property = QStringLiteral("frequency");
	frequency.value = QStringLiteral("2");
	QString error;
	expect(workbench.applyGraphEdit(frequency, &error), "a property edit through the graph applies", error);
	expect(workbench.currentText().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 2")), "the graph edit lands in the text");
	expect(workbench.hasUnsavedChanges(), "an edit marks the text unsaved");
	workbench.editor()->undo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1")), "Undo takes the graph edit back");
	workbench.editor()->redo();
	QCoreApplication::processEvents();
	expect(workbench.currentText().contains(QStringLiteral("0.5 0.5 0 2")), "Redo brings it again");

	// The property panel edits the selected node.
	graph->selectNode(QStringLiteral("stage/0/tcmod/0"));
	QCoreApplication::processEvents();
	auto* field = qobject_cast<QLineEdit*>(workbench.propertyPanel()->editorFor(QStringLiteral("s")));
	expect(field != nullptr, "a tcMod scroll node offers its S speed as a field");
	if (field) {
		field->setText(QStringLiteral("0.75"));
		Q_EMIT field->editingFinished();
		QCoreApplication::processEvents();
		expect(workbench.currentText().contains(QStringLiteral("tcMod scroll 0.75 0")), "editing a property field rewrites the directive",
			workbench.currentText());
	}

	// Typing in the text rebuilds the graph.
	QString text = workbench.currentText();
	text.replace(QStringLiteral("tcMod scroll 0.75 0"), QStringLiteral("tcMod scroll 0.75 0\n\t\ttcMod scale 2 2"));
	workbench.editor()->selectAll();
	workbench.editor()->insertPlainText(text);
	expect(waitFor([&]() { return hasNode(graph, QStringLiteral("stage/0/tcmod/1")); }, 10000), "a typed tcMod appears as a node");

	// Keyboard: Delete removes the selected node; arrows walk the wires.
	graph->setFocus();
	graph->selectNode(QStringLiteral("stage/0/tcmod/1"));
	QTest::keyClick(graph, Qt::Key_Delete);
	QCoreApplication::processEvents();
	expect(!workbench.currentText().contains(QStringLiteral("tcMod scale")), "Delete in the graph removes the node's directive");
	graph->selectNode(QStringLiteral("material"));
	// Right to left the graph flows the other way, and so do the keys.
	QTest::keyClick(graph, QApplication::layoutDirection() == Qt::RightToLeft ? Qt::Key_Right : Qt::Key_Left);
	expect(graph->selectedNode().startsWith(QStringLiteral("stage/")), "the leading arrow from the output walks to a stage feeding it",
		graph->selectedNode());
	QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(graph);
	expect(accessible && accessible->role() == QAccessible::List && accessible->childCount() == graph->nodeCount(),
		"the graph is a list of its nodes to screen readers");
	if (accessible && graph->selectedIndex() >= 0) {
		QAccessibleInterface* child = accessible->child(graph->selectedIndex());
		expect(child && child->text(QAccessible::Name) == graph->nodeAccessibleName(graph->selectedIndex()) && child->state().selected,
			"the selected node is a named, selected list item");
		expect(accessible->indexOfChild(child) == graph->selectedIndex(), "nodes know their place in the list");
	}

	// Filtering by the studio query language.
	workbench.filterField()->setText(QStringLiteral("animated=yes kind=shader"));
	QCoreApplication::processEvents();
	auto* proxy = qobject_cast<QSortFilterProxyModel*>(workbench.libraryView()->model());
	expect(proxy && proxy->rowCount() == 2, "animated=yes keeps the two animated shaders", proxy ? QString::number(proxy->rowCount()) : QString());
	workbench.filterField()->setText(QStringLiteral("nosuchkey=1"));
	QCoreApplication::processEvents();
	auto* count = workbench.findChild<QLabel*>(QStringLiteral("materialLibraryCount"));
	expect(count && count->text().contains(QStringLiteral("nosuchkey")), "a key no material has is named", count ? count->text() : QString());
	// From the Textures page: the materials that read an image.
	workbench.showMaterialsUsingImage(QStringLiteral("textures/test/glow.tga"));
	QCoreApplication::processEvents();
	expect(proxy && proxy->rowCount() == 2 && (workbench.currentMaterial() == QStringLiteral("textures/test/glow")
												   || workbench.currentMaterial() == QStringLiteral("textures/test/bad")),
		"an image lists the materials reading it", proxy ? QString::number(proxy->rowCount()) : QString());
	workbench.filterField()->clear();
	// From the Levels page: a Quake III map names shaders without textures/.
	workbench.revealMaterial(QStringLiteral("test/anim"));
	QCoreApplication::processEvents();
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/anim"), "a map's texture name finds its shader",
		workbench.currentMaterial());

	// A shader the engine drops is drawn as its fallback, with the reason.
	workbench.selectMaterial(QStringLiteral("textures/test/bad"));
	expect(rendered(), "the rejected shader renders");
	expect(workbench.preview()->lastResult().fallback, "Quake III's default shader stands in for a dropped shader");
	expect(workbench.problemsList()->topLevelItemCount() > 0, "the problems list explains it");

	// Staging a package script.
	workbench.selectMaterial(QStringLiteral("textures/test/glow"));
	expect(idle(), "back to the glow shader");
	staged.clear();
	expect(workbench.save(&error), "saving a package script stages it", error);
	expect(staged.contains(QStringLiteral("scripts/test.shader")) && stagedBytes.value(QStringLiteral("scripts/test.shader")).contains("0.5 0.5 0 2"),
		"the staged script holds the edits", staged.join(QLatin1Char(',')));
	expect(!workbench.hasUnsavedChanges(), "after saving nothing is unsaved");

	// New materials from templates.
	expect(workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/lamp"), QString(), &error),
		"a template adds a material", error);
	expect(workbench.currentMaterial() == QStringLiteral("textures/test/lamp") && workbench.currentText().contains(QStringLiteral("textures/test/lamp")),
		"the new material is selected in the text");
	expect(!workbench.addMaterialFromTemplate(QStringLiteral("q3-glow"), QStringLiteral("textures/test/lamp"), QString(), &error),
		"a name the script already defines is refused");
	workbench.revert();

	// A loose script saves in place.
	const QString loose = root.filePath(QStringLiteral("loose/scripts/mine.shader"));
	writeFile(loose, kShaders);
	expect(workbench.openScript(loose, &error), "a loose script opens", error);
	expect(idle(), "the loose script is read with the package");
	expect(workbench.currentDocumentTitle().contains(QStringLiteral("mine.shader")), "the loose script's own definitions are shown",
		workbench.currentDocumentTitle());
	MaterialGraphEdit blend;
	blend.kind = MaterialGraphEditKind::SetProperty;
	blend.node = QStringLiteral("stage/0");
	blend.property = QStringLiteral("blend");
	blend.value = QStringLiteral("add");
	if (workbench.currentMaterial() != QStringLiteral("textures/test/glow")) {
		workbench.selectMaterial(QStringLiteral("textures/test/glow"));
		idle();
	}
	expect(workbench.applyGraphEdit(blend, &error), "the stage's blend can be set from the graph", error);
	expect(workbench.save(&error), "the loose script saves", error);
	expect(readFile(loose).contains("blendFunc GL_ONE GL_ONE") || readFile(loose).contains("blendFunc add"), "the file on disk has the new blend",
		QString::fromUtf8(readFile(loose)));

	// Doom 3: interactions, and expressions as nodes.
	auto doom3 = openPackage(d3);
	workbench.setPackage(doom3, d3, QStringLiteral("d3-1"));
	expect(idle(), "the Doom 3 folder scans");
	expect(workbench.selectMaterial(QStringLiteral("textures/test/pulse")), "a Doom 3 material can be chosen");
	expect(rendered(), "the Doom 3 material renders");
	expect(workbench.currentTextKind() == MaterialTextKind::Doom3Material, "Doom 3 text is a material decl");
	MaterialGraphEdit wrap;
	wrap.kind = MaterialGraphEditKind::WrapExpression;
	wrap.node = QStringLiteral("stage/0/color/red/e");
	wrap.value = QStringLiteral("multiply");
	expect(workbench.applyGraphEdit(wrap, &error), "an expression can be wrapped in an operator", error);
	expect(workbench.currentText().contains(QStringLiteral("pulseTable[ time ] * ")) || workbench.currentText().contains(QStringLiteral("* ")),
		"the wrapped expression is written back", workbench.currentText());

	// Doom: animations as SWANTBLS, staged as Boom lumps.
	auto doom = openPackage(wad);
	workbench.setPackage(doom, wad, QStringLiteral("wad-1"));
	expect(idle(), "the Doom WAD scans");
	expect(workbench.selectMaterial(QStringLiteral("NUKAGE1")), "a Doom flat can be chosen");
	expect(workbench.currentTextKind() == MaterialTextKind::DoomSwantbls, "Doom animations are edited as SWANTBLS text");
	MaterialGraphEdit tics;
	tics.kind = MaterialGraphEditKind::SetProperty;
	tics.node = QStringLiteral("animation");
	tics.property = QStringLiteral("tics");
	tics.value = QStringLiteral("4");
	expect(workbench.applyGraphEdit(tics, &error), "the animation's speed is a property", error);
	expect(workbench.currentText().simplified().contains(QStringLiteral("4 NUKAGE3 NUKAGE1")), "the range's speed changes in the table");
	staged.clear();
	expect(workbench.save(&error), "saving Doom tables stages lumps", error);
	expect(staged.contains(QStringLiteral("lump:ANIMATED")) && staged.contains(QStringLiteral("lump:SWITCHES")), "ANIMATED and SWITCHES are staged",
		staged.join(QLatin1Char(',')));

	// Quake II: the WAL header as .wal_json.
	auto quake2 = openPackage(q2);
	workbench.setPackage(quake2, q2, QStringLiteral("q2-1"));
	expect(idle(), "the Quake II folder scans");
	expect(workbench.selectMaterial(QStringLiteral("e1u1/water1")), "a WAL can be chosen");
	expect(workbench.currentTextKind() == MaterialTextKind::Quake2WalJson, "a WAL header is edited as .wal_json");
	expect(rendered(), "the warped WAL renders");

	// Reduced motion: nothing starts playing by itself.
	workbench.setReducedMotion(true);
	workbench.setPackage(archive, q3, QStringLiteral("q3-2"));
	expect(idle(), "the package scans again");
	workbench.selectMaterial(QStringLiteral("textures/test/anim"));
	expect(idle(), "the animated shader loads");
	expect(!workbench.preview()->isPlaying(), "with reduced motion an animated material waits to be played");

	// Every control a screen reader meets has a name.
	for (QWidget* widget : workbench.findChildren<QWidget*>()) {
		if (!widget->isVisibleTo(&workbench) || widget->focusPolicy() == Qt::NoFocus) {
			continue;
		}
		if (qobject_cast<QLineEdit*>(widget) || widget->objectName().startsWith(QStringLiteral("material"))) {
			expect(!widget->accessibleName().isEmpty(), "focusable workbench controls have accessible names", widget->objectName());
		}
	}

	const QString snapshots = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS");
	if (!snapshots.isEmpty()) {
		workbench.setPackage(archive, q3, QStringLiteral("q3-3"));
		idle();
		workbench.selectMaterial(QStringLiteral("textures/test/glow"));
		rendered();
		workbench.grab().save(QDir(snapshots).filePath(QStringLiteral("material-workbench.png")));
	}

	if (failures > 0) {
		std::cerr << failures << " material workbench check(s) failed\n";
		return 1;
	}
	std::cout << "material workbench UI smoke passed\n";
	return 0;
}
