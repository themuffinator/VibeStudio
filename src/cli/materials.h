#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct MaterialsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// `material <action>`: list, inspect, validate, render, graph, edit,
// templates, new, doom-tables and wal, sharing core/material_* with the
// Materials page. `arguments` holds the whole command line after --cli.
MaterialsCliResult runMaterialCommand(const QString &action, const QStringList &arguments);
// The actions runMaterialCommand accepts, for routing.
QStringList materialCommandActions();
} // namespace vibestudio::cli
