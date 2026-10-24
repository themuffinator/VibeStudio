#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <filesystem>

namespace vibestudio::tests
{
// Only call for disposable fixtures created by the test, never installed data.
inline bool modelTestHardLink(const QString &source, const QString &alias)
{
#ifdef Q_OS_WIN
	const auto native = [](const QString &path) { return std::filesystem::path(path.toStdWString()); };
#else
	const auto native = [](const QString &path) { return std::filesystem::path(QFile::encodeName(path).constData()); };
#endif
	std::error_code error;
	std::filesystem::create_hard_link(native(source), native(alias), error);
	return !error;
}

inline QByteArray modelTestReadFile(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

template <typename Run, typename Check>
void checkModelAuxiliaryOutput(QStringList command, const QString &inputOption, Run run, Check check)
{
	const auto inputIndex = command.indexOf(inputOption) + 1, outputIndex = command.indexOf("--output") + 1;
	if (!check(inputIndex > 0 && outputIndex > 0, "prepare auxiliary input protection command"))
		return;
	const auto input = command[inputIndex];
	const auto bytes = modelTestReadFile(input);
	if (!command.contains("--overwrite"))
		command.append("--overwrite");
	for (int variant = 0; variant < 3; ++variant)
		for (bool dry : {true, false})
		{
			const auto fixture = input + QStringLiteral("-%1-%2.mesh.json").arg(variant).arg(dry);
			const auto alias = fixture + QStringLiteral("-alias.mesh.json");
			QFile file(fixture);
			if (!check(!bytes.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
					   "prepare independent auxiliary input fixture"))
				continue;
			file.close();
			if (!check(modelTestHardLink(fixture, alias), "create auxiliary input hard-link alias"))
				continue;
			command[inputIndex] = fixture;
			command[outputIndex] = variant == 0	  ? fixture
								   : variant == 1 ? QDir(QFileInfo(fixture).absolutePath()).filePath("./" + QFileInfo(fixture).fileName())
												  : alias;
			auto args = command;
			if (dry)
				args.append("--dry-run");
			const auto result = run(args);
			check(result.exitCode == 1 && modelTestReadFile(fixture) == bytes && modelTestReadFile(alias) == bytes,
				  "dry-run and write protect auxiliary inputs, alternate spellings and hard links even with overwrite");
		}
}
} // namespace vibestudio::tests
