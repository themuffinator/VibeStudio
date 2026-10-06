# VibeStudio Xiph decoder integration

Pinned from [ogg](https://github.com/xiph/ogg/tree/be05b13e98b048f0b5a0f5fa8ce514d56db5f822) at `be05b13e98b048f0b5a0f5fa8ce514d56db5f822`; reviewed 2026-10-04. BSD-3-Clause terms in COPYING are compatible with VibeStudio's GPLv3 licence. Original source and notices are unchanged.

Meson compiles these C sources into a private static dependency of the C++ audio importer. VibeStudio's forced-include allocation header redirects the upstream `_ogg_*` allocation macros to a thread-local bounded allocation region. Allocation failure returns through a guarded C library call; remaining region allocations are released without traversing a partial decoder state. All application logic remains C++20. Ogg system integer types are configured by the VibeStudio Meson adapter. There is no external codec installation, runtime download or device access.

The source hash manifest, this notice and COPYING ship in portable licence bundles. Test-only independent reference decoders are not shipped.
