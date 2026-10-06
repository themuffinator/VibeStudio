#include "app/package_progress.h"

#include <QCoreApplication>

#include <array>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
QString unisolated(QString text)
{
	return text.remove(QChar(0x2066)).remove(QChar(0x2069));
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto maximum = std::numeric_limits<quint64>::max();
	struct Case { quint64 done, total; const char* compact; int value; };
	const Case cases[] = {
		{0, 0, "0 B", 0}, {17, 0, "17 B", 0}, {1, 2, "1 / 2 B", 500},
		{0, 1048576, "0 / 1 MiB", 0}, {1, 1048576, "<0.01 / 1 MiB", 0},
		{65536, 131072, "64 / 128 KiB", 500}, {131071, 131072, "127.99 / 128 KiB", 999},
		{131072, 131072, "128 / 128 KiB", 1000}, {131072, 131073, "128 / 128.01 KiB", 999},
		{2049, 2048, "2.01 / 2 KiB", 1000}, {65536, 1048576, "0.06 / 1 MiB", 62},
		{maximum - 1, maximum, "15.99 / 16 EiB", 999}, {maximum, maximum, "16 / 16 EiB", 1000},
		{maximum, 0, "15.99 EiB", 0}
	};
	bool ok = true;
	for (const auto& item : cases) {
		const auto result = packageByteProgressText(item.done, item.total, QLocale::c());
		ok &= expect(unisolated(result.compact) == QString::fromLatin1(item.compact), "Compact quantities preserve useful units and distinguish partial from complete work.");
		ok &= expect(packageProgressValue(item.done, item.total) == item.value, "Progress range is truthful at zero, partial, complete and uint64 boundaries.");
	}
	int checked = 0;
	for (const auto& locale : {QLocale::c(), QLocale("en_GB"), QLocale("de_DE"), QLocale("ar_EG")}) {
		for (const auto total : std::array<quint64, 11>{1, 1023, 1024, 1025, 1048575, 1048576, 1048577,
			quint64(1) << 40, quint64(1) << 53, quint64(1) << 60, maximum}) {
			for (const auto done : {quint64(0), total / 2, total - 1, total}) {
				const auto result = packageByteProgressText(done, total, locale);
				ok &= expect(result.compact.startsWith(QChar(0x2066)) && result.compact.endsWith(QChar(0x2069)),
					"The entire quantity and unit preserve logical order inside RTL status text.");
				ok &= expect(result.exact.contains(locale.toString(done)) && result.exact.contains(locale.toString(total)),
					"Accessible byte counts retain exact uint64 values in the requested locale.");
				const auto values = unisolated(result.compact).section(' ', 0, -2).split(" / ");
				ok &= expect(values.size() == 2 && (done == total ? values[0] == values[1] : values[0] != values[1]),
					"Only a completed transfer displays equal quantities, including at unit boundaries.");
				ok &= expect(done == total ? packageProgressValue(done, total) == 1000 : packageProgressValue(done, total) < 1000,
					"No unfinished transfer fills the progress bar completely.");
				++checked;
			}
		}
		const auto unknown = packageByteProgressText(maximum, 0, locale);
		ok &= expect(unknown.exact.contains(locale.toString(maximum)) && unknown.exact.contains("total unknown")
			&& !unknown.compact.contains(" / "), "Unknown totals report a single exact count without a false zero denominator.");
	}
	std::cout << std::size(cases) << " quantity examples and " << checked << " localized boundary cases.\n";
	return ok ? 0 : 1;
}
