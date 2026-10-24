"""Prevent silent command omissions and unsupported parity claims in the audit."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import level_command_inventory as inventory


class Discovery(unittest.TestCase):
    def test_multiline_paths_literals_comments_and_lambdas(self):
        text = r'''
// addAction(Action{"not a command", QObject::tr("comment")});
/* addAction(Action{"also false", QObject::tr("comment")}); */
const char* decoy = "addAction(Action{fake})";
const char* raw = R"tag(
#if FAKE_GUARD
GlobalCommands_insert("false", {});
)tag";
#ifdef FEATURE
addAction(Action{
  std::filesystem::path("Menu/" /* keep concatenation */ "Split"),
  QObject::tr("Split \"Selection\""),
  Context::All, Key{}, [](auto& c) { c.run({1, 2}, "}"); }
});
#else
addAction(Action{R"(Menu/Alternative)", QObject::tr("Alternative"), [] {}});
#endif
'''
        rows = inventory.scan_tb("actions.cpp", text)
        self.assertEqual([r["command"] for r in rows], ["Menu/Split", "Menu/Alternative"])
        self.assertEqual(rows[0]["label"], 'Split "Selection"')
        self.assertEqual(rows[0]["locations"][0]["guards"], ["#ifdef FEATURE"])
        self.assertEqual(rows[1]["locations"][0]["guards"], ["#ifdef FEATURE; #else"])
        self.assertEqual(rows[0]["locations"][0]["line"], 10)

    def test_generated_families_and_unknown_expression(self):
        text = '''result.emplace_back(std::filesystem::path{"Tags/" + tag.name() + "/Enable"}, label);
result.emplace_back(std::filesystem::path{"Entities/" + definition.name + "/Create"}, label);
m_mainMenu.emplace_back(Menu{name, {}, {}});'''
        rows = inventory.scan_tb("actions.cpp", text)
        self.assertEqual([r["command"] for r in rows], ["Tags/{tag}/Enable", "Entities/{entity-definition}/Create"])
        self.assertTrue(all(r["kind"] == "family" for r in rows))
        with self.assertRaisesRegex(ValueError, "Unknown TB generated"):
            inventory.scan_tb("actions.cpp", text.replace("tag.name()", "unknown.name()"))
        with self.assertRaises(ValueError):
            inventory.scan_tb("actions.cpp", 'addAction(Action{dynamicPath, QObject::tr("Unknown")});')

    def test_nrc_filters_forwarding_and_unknown_variable(self):
        text = '''void add_filter_command(unsigned int flag, const char* command, Key key) {
GlobalToggles_insert(command, call, key);
}
add_filter_command(FLAG, "Filter" "Fixture", Key("Alt+1"));
// GlobalCommands_insert("Obsolete", call);
GlobalCommands_insert("Active", Caller<Object, void(), &Object::run>(object));
GlobalShortcuts_insert("Active", key);
'''
        rows = inventory.scan_nrc("radiant/filters.cpp", text)
        self.assertEqual([(r["command"], r["kind"]) for r in rows],
                         [("FilterFixture", "toggle"), ("Active", "command"), ("Active", "shortcut")])
        with self.assertRaisesRegex(ValueError, "unknown GlobalCommands_insert"):
            inventory.scan_nrc("radiant/new.cpp", "GlobalCommands_insert(name, callback);")
        with self.assertRaises(ValueError):
            inventory.scan_nrc("radiant/filters.cpp", 'GlobalCommands_insert("Broken", call;')

    def test_plugin_constants_namespaces_and_special_tokens(self):
        text = '''#define CMD_ONE "A;"
#define CMD_TWO "B"
const char *PLUGIN_COMMANDS = CMD_ONE "-;>;<;" CMD_TWO;
const char *QERPlug_GetCommandList() { return (char*)PLUGIN_COMMANDS; }
'''
        path = "contrib/example/plugin.cpp"
        rows = inventory.plugin_rows(path, text, {path})
        self.assertEqual([r["command"] for r in rows],
                         [f"plugin/{path}/{{command}}", f"plugin/{path}/A", f"plugin/{path}/B"])
        self.assertTrue(all(r["build"] == "listed-plugin-target" for r in rows))
        alternate = inventory.plugin_rows("contrib/example/legacy.cpp", text, {path})
        self.assertTrue(all(r["build"] == "source-only" for r in alternate))
        with self.assertRaises(ValueError):
            inventory.plugin_rows(path, text.replace("CMD_ONE \"-;>;<;\" CMD_TWO", "loadFromUnreviewedFile()"), set())

    def test_dynamic_sunplug_and_meshtex(self):
        rows = inventory.plugin_rows("contrib/sunplug/sunplug.cpp", '''
const char* getCommandList() {
const char first[] = "About";
const char second[] = ";Tool";
strcat(MenuList, first); strcat(MenuList, second);
return (const char*)MenuList;
}''', set())
        self.assertEqual([r.get("label") for r in rows[1:]], ["About", "Tool"])
        rows = inventory.plugin_rows("contrib/meshtex/MainMenu.cpp", '''
AddDialogShowEntry("Scale...", "Scale", dialog);
AddMeshVisitorEntry("Align", "Align", callback);
AddEntry("Help", "", callback);
AddEntry(commandLabel, command, callback);
''', {"contrib/meshtex/MainMenu.cpp"})
        self.assertEqual([r["command"].rsplit("/", 1)[1] for r in rows],
                         ["MeshTex.Scale", "MeshTex.Align", "MeshTex.Help"])


class Catalogue(unittest.TestCase):
    def setUp(self):
        self.catalogue = inventory.read_json(inventory.CATALOGUE)
        self.reviews = inventory.read_json(inventory.REVIEWS)

    def test_pinned_baseline_and_known_difficult_registrations(self):
        result = inventory.validate(self.catalogue, self.reviews)
        self.assertEqual(result["commands"], 596)
        self.assertEqual(result["categories"], {"tb.action": 179, "tb.family": 5,
                         "nrc.command": 222, "nrc.toggle": 68, "nrc.shortcut": 11,
                         "nrc.plugin-command": 96, "nrc.family": 15})
        self.assertFalse(result["parityComplete"])
        self.assertEqual(result["reviewed"] + result["unreviewed"], result["commands"])
        self.assertEqual(sum(result["statuses"].values()), result["reviewed"])
        rows = {r["id"]: r for r in self.catalogue["commands"]}
        self.assertGreater(len(rows["nrc:CameraForward"]["locations"]), 1)
        self.assertEqual(rows["nrc:FilterWorldBrushes"]["kind"], "toggle")
        self.assertEqual(rows["nrc:plugin/contrib/bobtoolz/ctfToolz-GTK.cpp/{command}"]["build"], "source-only")
        providers = [r for r in rows.values() if r["project"] == "nrc" and r["kind"] == "family"]
        self.assertEqual(sum(r["build"] == "listed-plugin-target" for r in providers), 8)
        self.assertTrue(any(r["locations"][0]["guards"] for r in rows.values() if r["project"] == "tb"))

    def test_stale_report_and_unreviewed_status_are_visible(self):
        report = inventory.markdown(self.catalogue, self.reviews)
        self.assertEqual(report, inventory.REPORT.read_text(encoding="utf-8"))
        self.assertIn("Reviewed statuses:", report)
        if inventory.validate(self.catalogue, self.reviews)["unreviewed"]:
            self.assertIn("| Unreviewed |", report)
        self.assertIn("## Remaining inventory layers", report)
        self.assertIn("#L", report)
        self.assertIn("Per-instance protected", report)

    def test_reject_duplicate_missing_unsafe_and_invalid_evidence(self):
        duplicate = deepcopy(self.catalogue)
        duplicate["commands"].append(duplicate["commands"][0])
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            inventory.validate(duplicate, self.reviews)
        for change in ({"id": "tb:NotRegistered"}, {"status": "Complete"},
                       {"evidence": ["src/tests/does-not-exist.cpp"]},
                       {"evidence": ["../other-project/private.cpp"]},
                       {"limitation": ""}, {"workflow": ""},
                       {"status": "Available", "gui": [], "cli": []}):
            with self.subTest(change=change):
                reviews = deepcopy(self.reviews)
                reviews["reviews"][0].update(change)
                with self.assertRaises(ValueError):
                    inventory.validate(self.catalogue, reviews)

    def test_not_applicable_needs_a_reason_but_no_route(self):
        reviews = deepcopy(self.reviews)
        before = inventory.validate(self.catalogue, reviews)["statuses"]["Not applicable"]
        self.assertNotEqual(reviews["reviews"][0]["status"], "Not applicable")
        reviews["reviews"][0].update({"status": "Not applicable", "gui": [], "cli": []})
        result = inventory.validate(self.catalogue, reviews)
        self.assertEqual(result["statuses"]["Not applicable"], before + 1)
        reviews["reviews"][0]["limitation"] = ""
        with self.assertRaises(ValueError):
            inventory.validate(self.catalogue, reviews)

    def test_route_validation_includes_shortcuts_and_rejects_stale_ids(self):
        gui = {s for r in self.reviews["reviews"] for s in r["gui"]}
        cli = {s for r in self.reviews["reviews"] for s in r["cli"]}
        ui_data = {"ok": True, "uiSemantics": {"commandPalette": [], "shortcuts": [{"commandId": s} for s in gui]}}
        cli_data = {"ok": True, "commands": [{"name": s} for s in cli]}
        inventory.validate(self.catalogue, self.reviews, ui=ui_data, cli=cli_data)
        reviews = deepcopy(self.reviews)
        reviews["reviews"][0]["gui"] = ["map.unregistered-action"]
        with self.assertRaisesRegex(ValueError, "Unknown palette route"):
            inventory.validate(self.catalogue, reviews, ui=ui_data)
        reviews["reviews"][0]["gui"] = []
        reviews["reviews"][0]["cli"] = ["map unregistered-action"]
        with self.assertRaisesRegex(ValueError, "Unknown CLI route"):
            inventory.validate(self.catalogue, reviews, cli=cli_data)
        with self.assertRaisesRegex(ValueError, "reports failure"):
            inventory.validate(self.catalogue, self.reviews, cli={"ok": False})
        with self.assertRaisesRegex(ValueError, "reports failure"):
            inventory.validate(self.catalogue, self.reviews, ui={})


if __name__ == "__main__":
    unittest.main()
