#include "app/application_shell.h"

#include "core/studio_manifest.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

QLabel* sectionLabel(const QString& text)
{
	auto* label = new QLabel(text);
	label->setObjectName("sectionLabel");
	return label;
}

QFrame* modulePanel(const StudioModule& module)
{
	auto* frame = new QFrame;
	frame->setObjectName("modulePanel");
	auto* layout = new QVBoxLayout(frame);
	layout->setContentsMargins(14, 12, 14, 12);
	layout->setSpacing(6);

	auto* name = new QLabel(module.name);
	name->setObjectName("moduleTitle");
	auto* meta = new QLabel(module.category + " / " + module.maturity);
	meta->setObjectName("moduleMeta");
	auto* description = new QLabel(module.description);
	description->setWordWrap(true);

	layout->addWidget(name);
	layout->addWidget(meta);
	layout->addWidget(description);
	layout->addStretch(1);
	return frame;
}

} // namespace

ApplicationShell::ApplicationShell(QWidget* parent)
	: QMainWindow(parent)
{
	setWindowTitle("VibeStudio");
	resize(1280, 800);

	auto* root = new QWidget;
	auto* rootLayout = new QHBoxLayout(root);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	auto* rail = new QListWidget;
	rail->setObjectName("modeRail");
	rail->setFixedWidth(190);
	rail->addItems({"Workspace", "Levels", "Models", "Textures", "Audio", "Packages", "Code", "Shaders", "Build"});
	rootLayout->addWidget(rail);

	auto* splitter = new QSplitter(Qt::Horizontal);
	rootLayout->addWidget(splitter, 1);

	auto* center = new QWidget;
	auto* centerLayout = new QVBoxLayout(center);
	centerLayout->setContentsMargins(22, 18, 22, 18);
	centerLayout->setSpacing(16);

	auto* topRow = new QHBoxLayout;
	auto* title = new QLabel("VibeStudio");
	title->setObjectName("appTitle");
	auto* subtitle = new QLabel("Integrated development studio for idTech1-3 projects");
	subtitle->setObjectName("appSubtitle");
	auto* titleStack = new QVBoxLayout;
	titleStack->addWidget(title);
	titleStack->addWidget(subtitle);
	topRow->addLayout(titleStack, 1);
	topRow->addWidget(new QPushButton("Open Project"));
	topRow->addWidget(new QPushButton("Detect Installs"));
	centerLayout->addLayout(topRow);

	centerLayout->addWidget(sectionLabel("Studio Surface"));

	auto* grid = new QGridLayout;
	grid->setSpacing(12);
	const QVector<StudioModule> modules = plannedModules();
	for (int index = 0; index < modules.size(); ++index) {
		grid->addWidget(modulePanel(modules[index]), index / 2, index % 2);
	}
	centerLayout->addLayout(grid);
	centerLayout->addStretch(1);
	splitter->addWidget(center);

	auto* inspector = new QTextEdit;
	inspector->setObjectName("inspector");
	inspector->setReadOnly(true);
	inspector->setText("Compiler imports and project diagnostics will surface here.\n\nRun `vibestudio --cli --compiler-report` for the current imported toolchain manifest.");
	splitter->addWidget(inspector);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	setCentralWidget(root);
	statusBar()->showMessage("Scaffold ready");

	setStyleSheet(R"(
		QMainWindow, QWidget {
			background: #17191c;
			color: #e8edf2;
			font-size: 10.5pt;
		}
		QListWidget#modeRail {
			background: #101215;
			border: 0;
			padding: 12px 8px;
		}
		QListWidget#modeRail::item {
			padding: 9px 10px;
			border-radius: 4px;
		}
		QListWidget#modeRail::item:selected {
			background: #2d5f88;
			color: #ffffff;
		}
		QLabel#appTitle {
			font-size: 24pt;
			font-weight: 700;
		}
		QLabel#appSubtitle, QLabel#moduleMeta {
			color: #9daab8;
		}
		QLabel#sectionLabel {
			color: #d6dde5;
			font-size: 13pt;
			font-weight: 600;
		}
		QFrame#modulePanel, QTextEdit#inspector {
			background: #20242a;
			border: 1px solid #323943;
			border-radius: 6px;
		}
		QLabel#moduleTitle {
			font-size: 12.5pt;
			font-weight: 700;
		}
		QPushButton {
			background: #2d5f88;
			border: 1px solid #447aa8;
			border-radius: 4px;
			color: #ffffff;
			padding: 7px 12px;
		}
		QPushButton:hover {
			background: #386f9c;
		}
		QStatusBar {
			background: #101215;
		}
	)");
}

} // namespace vibestudio
