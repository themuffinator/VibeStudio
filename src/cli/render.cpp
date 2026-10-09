#include "cli/render.h"

#include "core/render_device.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(RenderCli)
};

// CliExitCode values (cli/cli.cpp).
constexpr int kFailure = 1;
constexpr int kUsage = 2;
constexpr int kValidationFailed = 4;
constexpr int kUnavailable = 5;

RenderCliResult fail(int code, const QString& message)
{
	RenderCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}

struct Arguments {
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
};

// The global flags and options every CLI module accepts, plus --renderer.
// Positional words after the family and action are the command's own.
bool parseArguments(const QStringList& arguments, Arguments* parsed, QString* error)
{
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--no-color")};
	const QSet<QString> options {QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root"),
		QStringLiteral("--renderer")};
	for (int i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments.at(i);
		if (!argument.startsWith(QLatin1Char('-'))) {
			parsed->positional.append(argument);
			continue;
		}
		const qsizetype equal = argument.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? argument : argument.left(equal);
		if (parsed->seen.contains(key)) {
			*error = Text::tr("Repeated option: %1.").arg(key);
			return false;
		}
		parsed->seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!options.contains(key)) {
			*error = Text::tr("Unknown or invalid option: %1.").arg(key);
			return false;
		}
		const QString value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--"))) {
			*error = Text::tr("Option %1 requires a value.").arg(key);
			return false;
		}
		parsed->values.insert(key, value);
	}
	// The family and the action.
	parsed->positional = parsed->positional.mid(2);
	return true;
}

QString choiceIds()
{
	QStringList ids;
	for (RenderBackendChoice choice : renderBackendChoices()) {
		ids << renderBackendChoiceId(choice);
	}
	return ids.join(QStringLiteral(", "));
}

QString deviceTypeText(const QString& type)
{
	if (type == QStringLiteral("discrete-gpu")) {
		return Text::tr("discrete GPU");
	}
	if (type == QStringLiteral("integrated-gpu")) {
		return Text::tr("integrated GPU");
	}
	if (type == QStringLiteral("virtual-gpu")) {
		return Text::tr("virtual GPU");
	}
	if (type == QStringLiteral("cpu")) {
		return Text::tr("runs on the processor");
	}
	return Text::tr("other device");
}

QJsonObject deviceJson(const RenderDeviceInfo& info)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), renderBackendId(info.backend));
	object.insert(QStringLiteral("name"), renderBackendDisplayName(info.backend));
	object.insert(QStringLiteral("started"), info.started);
	object.insert(QStringLiteral("available"), info.available);
	object.insert(QStringLiteral("apiVersion"), info.apiVersion);
	object.insert(QStringLiteral("deviceName"), info.deviceName);
	object.insert(QStringLiteral("vendor"), info.vendor);
	object.insert(QStringLiteral("driverVersion"), info.driverVersion);
	object.insert(QStringLiteral("deviceType"), info.deviceType);
	object.insert(QStringLiteral("softwareImplementation"), info.softwareImplementation);
	object.insert(QStringLiteral("maxTextureSize"), info.maxTextureSize);
	object.insert(QStringLiteral("startupMilliseconds"), static_cast<double>(info.startupMilliseconds));
	object.insert(QStringLiteral("summary"), renderDeviceSummary(info));
	object.insert(QStringLiteral("error"), info.error);
	object.insert(QStringLiteral("errorDetail"), info.errorDetail);
	return object;
}

QString deviceLine(const RenderDeviceInfo& info)
{
	const QString name = renderBackendDisplayName(info.backend);
	if (!info.available) {
		QString line = Text::tr("%1: unavailable. %2").arg(name, info.error);
		if (!info.errorDetail.isEmpty()) {
			line += QLatin1Char(' ') + Text::tr("(%1)", "untranslated technical detail").arg(info.errorDetail);
		}
		return line;
	}
	// OpenGL cannot tell a discrete GPU from an integrated one; say nothing
	// rather than "other".
	const QString device = info.deviceType == QStringLiteral("other")
		? info.deviceName
		: Text::tr("%1 (%2)", "graphics device, then its kind").arg(info.deviceName, deviceTypeText(info.deviceType));
	return Text::tr("%1: available. %2 on %3, driver %4, textures up to %5 pixels.")
		.arg(name, info.apiVersion, device, info.driverVersion.isEmpty() ? Text::tr("unknown") : info.driverVersion)
		.arg(info.maxTextureSize);
}

// The choice, where it comes from, and what it resolves to.
void describeChoice(RenderCliResult* result, const StudioSettings& settings)
{
	const QString source = renderBackendChoiceSource();
	result->payload.insert(QStringLiteral("choice"), renderBackendChoiceId(renderBackendChoice()));
	result->payload.insert(QStringLiteral("choiceSource"), source);
	result->payload.insert(QStringLiteral("preference"), settings.renderBackendPreference());
	QJsonArray order;
	for (RenderBackend backend : automaticRenderBackendOrder()) {
		order.append(renderBackendId(backend));
	}
	result->payload.insert(QStringLiteral("automaticOrder"), order);
	QString from;
	if (source == QStringLiteral("environment")) {
		from = Text::tr("set by VIBESTUDIO_RENDER_BACKEND");
	} else if (source == QStringLiteral("command-line")) {
		from = Text::tr("set by --renderer for this run");
	} else {
		from = Text::tr("the saved preference");
	}
	result->lines << Text::tr("Renderer choice: %1 (%2).").arg(renderBackendChoiceDisplayName(renderBackendChoice()), from);
}

RenderCliResult runBackends(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, &args, &error)) {
		return fail(kUsage, error);
	}
	if (!args.positional.isEmpty()) {
		return fail(kUsage, Text::tr("render backends takes no paths or names; use --renderer to try one renderer."));
	}
	if (!applyRendererChoice(arguments, &error)) {
		return fail(kUsage, error);
	}
	RenderCliResult result;
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	describeChoice(&result, settings);
	QJsonArray backends;
	for (const RenderDeviceInfo& info : probeRenderBackends()) {
		backends.append(deviceJson(info));
		result.lines << QStringLiteral("  ") + deviceLine(info);
	}
	result.payload.insert(QStringLiteral("backends"), backends);
	RenderDeviceInfo failure;
	const std::shared_ptr<RenderDevice> active = activeRenderDevice(&failure);
	if (!active) {
		result.payload.insert(QStringLiteral("active"), QJsonValue());
		result.exitCode = kUnavailable;
		result.error = failure.error;
		return result;
	}
	const RenderDeviceInfo info = active->info();
	result.payload.insert(QStringLiteral("active"), renderBackendId(info.backend));
	result.payload.insert(QStringLiteral("activeSummary"), renderDeviceSummary(info));
	result.lines.prepend(Text::tr("3D views draw with %1.").arg(renderDeviceSummary(info)));
	return result;
}

RenderCliResult runTest(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, &args, &error)) {
		return fail(kUsage, error);
	}
	if (!args.positional.isEmpty()) {
		return fail(kUsage, Text::tr("render test takes no paths or names; use --renderer to test one renderer."));
	}
	if (!applyRendererChoice(arguments, &error)) {
		return fail(kUsage, error);
	}
	// --renderer opengl or vulkan tests that one; otherwise every backend.
	QVector<RenderBackend> backends = renderBackends();
	const RenderBackendChoice choice = renderBackendChoice();
	if (args.values.contains(QStringLiteral("--renderer")) && choice != RenderBackendChoice::Automatic) {
		backends = {choice == RenderBackendChoice::OpenGL ? RenderBackend::OpenGL : RenderBackend::Vulkan};
	}
	RenderCliResult result;
	QJsonArray tests;
	int available = 0;
	int failed = 0;
	for (RenderBackend backend : std::as_const(backends)) {
		const std::shared_ptr<RenderDevice> device = renderDevice(backend);
		const RenderDeviceInfo info = device->info();
		QJsonObject test = deviceJson(info);
		const QString name = renderBackendDisplayName(backend);
		if (!info.available) {
			test.insert(QStringLiteral("tested"), false);
			test.insert(QStringLiteral("passed"), false);
			result.lines << Text::tr("%1: not tested, unavailable. %2").arg(name, info.error);
			tests.append(test);
			continue;
		}
		++available;
		const RenderSelfTestResult outcome = runRenderSelfTest(*device);
		test.insert(QStringLiteral("tested"), true);
		test.insert(QStringLiteral("passed"), outcome.passed);
		test.insert(QStringLiteral("milliseconds"), std::round(outcome.milliseconds * 100.0) / 100.0);
		test.insert(QStringLiteral("testError"), outcome.error);
		test.insert(QStringLiteral("testErrorDetail"), outcome.errorDetail);
		tests.append(test);
		if (outcome.passed) {
			result.lines << Text::tr("%1: passed in %2 ms on %3.").arg(name).arg(outcome.milliseconds, 0, 'f', 1).arg(renderDeviceSummary(info));
		} else {
			++failed;
			result.lines << Text::tr("%1: failed. %2 (%3)").arg(name, outcome.error, outcome.errorDetail);
		}
	}
	result.payload.insert(QStringLiteral("tests"), tests);
	result.payload.insert(QStringLiteral("available"), available);
	result.payload.insert(QStringLiteral("failed"), failed);
	if (available == 0) {
		result.exitCode = kUnavailable;
		result.error = Text::tr("No 3D renderer could start, so nothing was tested.");
	} else if (failed > 0) {
		result.exitCode = kValidationFailed;
		result.error = Text::tr("%n renderer(s) drew the test image wrongly.", nullptr, failed);
	}
	return result;
}

RenderCliResult runSet(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, &args, &error)) {
		return fail(kUsage, error);
	}
	if (args.positional.size() != 1) {
		return fail(kUsage, Text::tr("Name the renderer to save: %1.").arg(choiceIds()));
	}
	RenderBackendChoice choice = RenderBackendChoice::Automatic;
	if (!renderBackendChoiceFromId(args.positional.first(), &choice)) {
		return fail(kUsage, Text::tr("Unknown renderer %1; use %2.").arg(args.positional.first(), choiceIds()));
	}
	StudioSettings settings;
	settings.setRenderBackendPreference(renderBackendChoiceId(choice));
	settings.sync();
	if (settings.isReadOnly() || settings.discardedWriteCount() > 0 || settings.status() != QSettings::NoError) {
		return fail(kFailure, Text::tr("The renderer preference could not be saved to %1.").arg(settings.storageLocation()));
	}
	RenderCliResult result;
	result.payload.insert(QStringLiteral("preference"), renderBackendChoiceId(choice));
	result.payload.insert(QStringLiteral("storage"), settings.storageLocation());
	result.payload.insert(QStringLiteral("overriddenByEnvironment"), renderBackendChoiceSource() == QStringLiteral("environment"));
	result.lines << Text::tr("Saved the 3D renderer: %1. The studio uses it from its next frame.").arg(renderBackendChoiceDisplayName(choice));
	if (renderBackendChoiceSource() == QStringLiteral("environment")) {
		result.lines << Text::tr("VIBESTUDIO_RENDER_BACKEND is set, and chooses for any run while it is.");
	}
	return result;
}

} // namespace

bool applyRendererChoice(const QStringList& arguments, QString* error)
{
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	RenderBackendChoice saved = RenderBackendChoice::Automatic;
	renderBackendChoiceFromId(settings.renderBackendPreference(), &saved);
	setRenderBackendChoice(saved);
	for (int i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments.at(i);
		QString value;
		if (argument == QStringLiteral("--renderer")) {
			value = arguments.value(i + 1);
		} else if (argument.startsWith(QStringLiteral("--renderer="))) {
			value = argument.mid(11);
		} else {
			continue;
		}
		RenderBackendChoice choice = RenderBackendChoice::Automatic;
		if (!renderBackendChoiceFromId(value, &choice)) {
			if (error) {
				*error = Text::tr("Unknown renderer %1; use %2.").arg(value, choiceIds());
			}
			return false;
		}
		setRenderBackendChoiceOverride(choice);
		break;
	}
	return true;
}

QStringList renderCommandActions()
{
	return {QStringLiteral("backends"), QStringLiteral("test"), QStringLiteral("set")};
}

RenderCliResult runRenderCommand(const QString& action, const QStringList& arguments)
{
	if (action == QStringLiteral("backends") || action == QStringLiteral("list") || action == QStringLiteral("status")) {
		return runBackends(arguments);
	}
	if (action == QStringLiteral("test") || action == QStringLiteral("check")) {
		return runTest(arguments);
	}
	if (action == QStringLiteral("set")) {
		return runSet(arguments);
	}
	return fail(kUsage, Text::tr("Unknown render action %1; use %2.").arg(action, renderCommandActions().join(QStringLiteral(", "))));
}

} // namespace vibestudio::cli
