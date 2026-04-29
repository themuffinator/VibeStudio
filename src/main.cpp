#include "app/application_shell.h"
#include "cli/cli.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

namespace {

QStringList rawArguments(int argc, char** argv)
{
	QStringList args;
	for (int index = 0; index < argc; ++index) {
		args.push_back(QString::fromLocal8Bit(argv[index]));
	}
	return args;
}

} // namespace

int main(int argc, char** argv)
{
	const QStringList args = rawArguments(argc, argv);
	if (args.contains("--cli")) {
		QCoreApplication app(argc, argv);
		return vibestudio::cli::run(app.arguments());
	}

	QApplication app(argc, argv);
	vibestudio::ApplicationShell shell;
	shell.show();
	return app.exec();
}
