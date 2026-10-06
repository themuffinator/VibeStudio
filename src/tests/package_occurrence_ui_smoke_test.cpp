#include "app/package_extraction_paths.h"
#include "app/package_operation_dialog.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
void u32(QByteArray& bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) { bytes.append(static_cast<char>(value >> shift)); }
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return expanded && QByteArray(context) == "VibeStudioPackagePaths"
			? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	// Direct widget properties and QWidget::render; no native input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path());
	QByteArray wad("PWAD"); u32(wad, 4); u32(wad, 20); wad.append("AAAABBBB");
	for (int index = 0; index < 4; ++index) {
		u32(wad, index < 2 ? 12 : 16); u32(wad, index % 2 ? 4 : 0);
		QByteArray name = index % 2 ? QByteArray("THINGS") : index ? QByteArray("GROUPB") : QByteArray("GROUPA");
		name.resize(8, '\0'); wad.append(name);
	}
	const QString sourcePath = root.filePath(QStringLiteral("maps.wad"));
	const QString replacementPath = root.filePath(QStringLiteral("replacement.bin"));
	PackageArchive source; PackageStagingModel plan; QString error;
	bool ok = write(sourcePath, wad) && write(replacementPath, "CCCC") && source.load(sourcePath, &error) && plan.loadBaseArchive(source, &error);
	if (!expect(ok, "prepare repeated-name fixture")) { return 1; }
	QVector<qsizetype> indexes;
	for (qsizetype index = 0; index < source.entries().size(); ++index) {
		if (source.entries().at(index).virtualPath == QStringLiteral("THINGS")) { indexes << index; }
	}
	if (!expect(indexes.size() == 2, "both repeated entries have indexes")) { return 1; }
	const auto staged = runPackageStagingDialog(nullptr, plan, {{replacementPath, QStringLiteral("THINGS"), PackageStageConflictResolution::Block, true, 3}});
	PackageStagingArchive stagedView(staged.staging);
	QByteArray replaced, untouched;
	for (qsizetype index = 0; index < stagedView.entries().size(); ++index) {
		const auto entry = stagedView.entries().at(index);
		if (entry.sourceOrdinal == 1) { stagedView.readEntryAt(index, &untouched, &error); }
		if (entry.sourceOrdinal == 3) { stagedView.readEntryAt(index, &replaced, &error); }
	}
	ok &= expect(staged.accepted == 1 && staged.errors.isEmpty() && replaced == "CCCC" && untouched == "AAAA"
		&& staged.staging.operations().last().sourceOrdinal == 3 && plan.operations().isEmpty(), "staging worker replaces only the selected occurrence");

	ExpandedTranslator translator; app.installTranslator(&translator);
	for (const int scale : {100, 200}) {
		translator.expanded = scale == 200;
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		PackageExtractionRequest request;
		request.targetDirectory = root.filePath(QStringLiteral("mapped-%1").arg(scale));
		request.entrySelections = {{indexes.at(0), {}}, {indexes.at(1), {}}};
		bool visited = false;
		QTimer::singleShot(0, [&]() {
			auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
			if (!dialog) { ok &= expect(false, "mapping dialog is modal"); return; }
			visited = true;
			auto* paths = dialog->findChild<QTreeWidget*>(QStringLiteral("extractionPaths"));
			auto* extract = dialog->findChild<QPushButton*>(QStringLiteral("extractMappedEntries"));
			auto* status = dialog->findChild<QLabel*>(QStringLiteral("extractionPathStatus"));
			if (!paths || paths->topLevelItemCount() != 2 || !extract || !status) { ok &= expect(false, "mapping controls exist"); dialog->reject(); return; }
			ok &= expect(paths->focusPolicy() != Qt::NoFocus && !paths->accessibleName().isEmpty()
				&& !status->accessibleName().isEmpty() && extract->isEnabled(), "accessible mapping controls start with distinct suggested paths");
			auto* first = paths->topLevelItem(0); auto* second = paths->topLevelItem(1);
			ok &= expect(first->text(0) != second->text(0) && first->data(0, Qt::AccessibleTextRole) != second->data(0, Qt::AccessibleTextRole), "source rows retain distinct spoken identities");
			second->setText(1, first->text(1));
			ok &= expect(!extract->isEnabled() && !second->toolTip(1).isEmpty(), "duplicate outputs block acceptance with a row diagnostic");
			second->setText(1, QStringLiteral("../escape"));
			ok &= expect(!extract->isEnabled(), "unsafe output paths block acceptance");
			first->setText(1, QStringLiteral("map-one/THINGS"));
			second->setText(1, QStringLiteral("map-one/THINGS/child"));
			ok &= expect(!extract->isEnabled(), "file and required directory collisions block acceptance");
			second->setText(1, QStringLiteral("map-two/THINGS"));
			ok &= expect(extract->isEnabled() && dialog->layoutDirection() == app.layoutDirection(), "valid mapping can be accepted in either layout direction");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				dialog->ensurePolished(); app.processEvents();
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
				ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-extraction-paths-%1.png").arg(scale))), "render mapping dialog");
			}
			extract->click();
		});
		const bool accepted = reviewPackageExtractionPaths(nullptr, source, &request);
		ok &= expect(visited && accepted && request.entrySelections.size() == 2 && !request.extractAll, "review returns exact mapped selections");
		ok &= expect(extractPackageEntries(source, request).succeeded()
			&& read(QDir(request.targetDirectory).filePath(QStringLiteral("map-one/THINGS"))) == "AAAA"
			&& read(QDir(request.targetDirectory).filePath(QStringLiteral("map-two/THINGS"))) == "BBBB", "mapped GUI request extracts both original payloads");
	}
	app.removeTranslator(&translator);
	PackageExtractionRequest cancelled; cancelled.extractAll = true;
	bool cancelledDialog = false;
	QTimer::singleShot(0, [&]() {
		if (auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget())) { cancelledDialog = true; dialog->reject(); }
	});
	ok &= expect(!reviewPackageExtractionPaths(nullptr, source, &cancelled) && cancelledDialog
		&& cancelled.extractAll && cancelled.entrySelections.isEmpty(), "cancelling preserves the original request");
	PackageExtractionRequest one; one.entrySelections = {{indexes.at(1), {}}};
	ok &= expect(reviewPackageExtractionPaths(nullptr, source, &one) && one.entrySelections.at(0).outputVirtualPath.isEmpty(), "one selected occurrence needs no mapping dialog");

	if (!expect(argc >= 2, "CLI binary is supplied by Meson")) { return 1; }
	const auto cli = [&](const QStringList& arguments, int expectedExit = 0) {
		QProcess process;
		process.setWorkingDirectory(root.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("package")}
			+ arguments + QStringList{QStringLiteral("--json")});
		const bool ended = process.waitForFinished(15000);
		const QByteArray output = process.readAllStandardOutput();
		if (!ended || process.exitCode() != expectedExit) { std::cerr << arguments.join(' ').toStdString() << '\n' << output.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "occurrence CLI exit status matches the operation");
		return QJsonDocument::fromJson(output).object();
	};
	const auto listed = cli({"list", sourcePath}).value("package").toObject().value("entries").toArray();
	QHash<int, QString> cliIndexes;
	for (const auto& row : listed) { const auto entry = row.toObject(); cliIndexes.insert(entry.value("sourceOrdinal").toInt(-1), QString::number(entry.value("entryIndex").toInt(-1))); }
	if (!expect(cliIndexes.contains(1) && cliIndexes.contains(3), "CLI list provides physical ordinals and snapshot indexes")) { return 1; }
	const QString firstIndex = cliIndexes.value(1), secondIndex = cliIndexes.value(3);
	const auto preview = cli({"preview", sourcePath, "--entry-index", secondIndex}).value("preview").toObject();
	ok &= expect(preview.value("body").toString().contains("BBBB") && !preview.value("body").toString().contains("AAAA"), "CLI index preview resolves the requested repeated payload");
	cli({"preview", sourcePath, "THINGS"}, 3);
	cli({"preview", sourcePath, "THINGS", "--entry-index", secondIndex}, 2);
	cli({"preview", sourcePath, "--entry-index=1.5"}, 2);
	cli({"preview", sourcePath, "--entry-index", "--quiet"}, 2);
	cli({"preview", sourcePath, "--entry-index", secondIndex, "--unknown"}, 2);
	cli({"preview", sourcePath, "--entry-index", "999"}, 3);
	const QString extractPath = root.filePath(QStringLiteral("cli-extracted"));
	const QStringList mapped{"extract", sourcePath, "--output", extractPath, "--entry-index", firstIndex, "--as", "first/THINGS", "--entry-index", secondIndex, "--as", "second/THINGS"};
	cli(mapped + QStringList{"--dry-run"});
	ok &= expect(!QFileInfo::exists(extractPath), "indexed dry run creates no output directories");
	cli(mapped);
	ok &= expect(read(QDir(extractPath).filePath("first/THINGS")) == "AAAA" && read(QDir(extractPath).filePath("second/THINGS")) == "BBBB", "CLI mapped extraction preserves both occurrences");
	const QString rejectedPath = root.filePath(QStringLiteral("rejected-extraction"));
	const QStringList extraction{"extract", sourcePath, "--output", rejectedPath};
	cli(extraction + QStringList{"--entry-index", firstIndex, "--entry-index", firstIndex}, 2);
	cli(extraction + QStringList{"--entry-index", firstIndex, "--entry-index", secondIndex, "--as", "only-one"}, 2);
	cli(extraction + QStringList{"--entry-index", secondIndex, "--extract-all"}, 2);
	cli(extraction + QStringList{"--entry-index="}, 2);
	cli(extraction + QStringList{"--as", "orphan"}, 2);
	cli(extraction + QStringList{"--entry-index", "999"}, 1);
	cli(extraction + QStringList{"--entry-index", secondIndex, "--as", "../escape"}, 1);
	ok &= expect(!QFileInfo::exists(rejectedPath), "invalid indexed selections never broaden into extract-all");
	const QString draft = root.filePath(QStringLiteral("edited.vibepackage"));
	cli({"draft-save", sourcePath, draft, "--replace-ordinal", "3", "--replace-file", replacementPath, "--rename-ordinal", "1", "--to", "ACTORS"});
	PackageStagingModel restored;
	ok &= expect(PackageDraft::load(draft, &restored, &error) && restored.operations().size() == 2
		&& restored.operations().at(0).sourceOrdinal == 3 && restored.operations().at(1).sourceOrdinal == 1, "CLI draft persists occurrence identity");
	ok &= expect(cli({"preview", draft, "THINGS"}).value("preview").toObject().value("body").toString().contains("CCCC"), "draft preview sees the selected replacement");
	cli({"draft-undo", draft});
	ok &= expect(PackageDraft::load(draft, &restored, &error) && restored.operations().isEmpty() && restored.canRedo(), "CLI undo restores the whole edit group");
	cli({"draft-redo", draft});
	const QString saved = root.filePath(QStringLiteral("selected.wad"));
	cli({"save-as", draft, saved, "--delete-ordinal", "3"});
	PackageArchive exported;
	ok &= expect(exported.load(saved, &error) && exported.entries().size() == 3, "CLI save-as deletes only the selected occurrence");
	const QString rejectedOutput = root.filePath(QStringLiteral("bad.wad"));
	cli({"save-as", sourcePath, rejectedOutput, "--delete-ordinal="}, 2);
	cli({"save-as", sourcePath, rejectedOutput, "--delete-ordinal"}, 2);
	ok &= expect(!QFileInfo::exists(rejectedOutput), "missing selectors cannot silently save an unchanged archive");
	const QString rejectedDraft = root.filePath(QStringLiteral("bad.vibepackage"));
	cli({"draft-save", sourcePath, rejectedDraft, "--replace-ordinal", "3"}, 2);
	cli({"draft-save", sourcePath, rejectedDraft, "--replace-ordinal", "3", "--replace-entry", "THINGS", "--replace-file", replacementPath}, 2);
	cli({"draft-save", sourcePath, rejectedDraft, "--rename-ordinal", "1", "--rename", "THINGS", "--to", "ACTORS"}, 2);
	cli({"draft-save", sourcePath, rejectedDraft, "--delete-ordinal=-1"}, 2);
	cli({"draft-save", sourcePath, rejectedDraft, "--delete-ordinal=999"}, 2);
	ok &= expect(!QFileInfo::exists(rejectedDraft), "rejected occurrence edits create no draft");
	return ok ? 0 : 1;
}
