"""Project-authored binary fixtures; no archive maps, assets or external code."""

import copy
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))

from tools.map_study.binary import Aas, Bsp, FormatError, entities, sha256
from tools.map_study.corpus import assign_splits, build_profile, is_link, metadata
from tools.map_study.features import analyze, navigation, strongly_connected
from tools.map_study.__main__ import compare_metrics
from tools.map_study.render import dossier


def lump_file(magic, version, lumps, aas=False):
    header_size = (12 if aas else 8) + 8 * len(lumps)
    body = b""
    entries = []
    for lump in lumps:
        entries.append((header_size + len(body), len(lump)))
        body += lump
    header = bytearray(struct.pack("<4si", magic, version) + (struct.pack("<I", 0) if aas else b"") +
                       b"".join(struct.pack("<ii", *entry) for entry in entries))
    if aas and version == 5:
        for i in range(8, len(header)):
            header[i] ^= ((i - 8) * 119) & 255
    return bytes(header) + body


def fixture_bsp(wall=True, extra=b""):
    lumps = [b""] * 19
    lumps[0] = (b'{"classname" "worldspawn" "message" "Test <map>"}\n'
                b'{"classname" "info_player_deathmatch" "origin" "0 0 24"}\n'
                b'{"classname" "info_player_deathmatch" "origin" "160 0 24"}\n'
                b'{"classname" "weapon_railgun" "origin" "192 0 24"}\n' + extra)
    planes = [(1, 0, 0, 80), (-1, 0, 0, -64), (0, 1, 0, 128),
              (0, -1, 0, 128), (0, 0, 1, 128), (0, 0, -1, 64), (0, 0, 1, 0)]
    lumps[1] = b"".join(struct.pack("<4fi", *p, 0) for p in planes)
    vertices = [(-128, -128, 0), (256, -128, 0), (256, 128, 0), (-128, 128, 0)]
    lumps[2] = b"".join(struct.pack("<3f", *v) for v in vertices)
    lumps[4] = struct.pack("<iii6hHH", 0, -2, -1, -128, -128, -64, 256, 128, 128, 0, 1)
    lumps[5] = struct.pack("<8fii32si", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, b"test/stone", -1)
    lumps[6] = struct.pack("<Hhihh4Bi", 6, 0, 0, 4, 0, 255, 255, 255, 255, -1)
    lumps[8] = b"".join(struct.pack("<ihh6h4H", 0, 0, 0, -128, -128, -64, 256, 128, 128,
                                   0, 1, 0, 1 if wall else 0) for _ in range(2))
    lumps[9] = struct.pack("<H", 0)
    lumps[10] = struct.pack("<H", 0) if wall else b""
    lumps[11] = b"".join(struct.pack("<HH", *e) for e in ((0, 0), (0, 1), (1, 2), (2, 3), (3, 0)))
    lumps[12] = struct.pack("<4i", 1, 2, 3, 4)
    lumps[13] = struct.pack("<9f3i", -128, -128, -64, 256, 128, 128, 0, 0, 0, 0, 0, 1)
    lumps[14] = struct.pack("<iii", 0, 6, 1) if wall else b""
    lumps[15] = b"".join(struct.pack("<Hh", i, 0) for i in range(6)) if wall else b""
    return lump_file(b"IBSP", 38, lumps)


def fixture_aas(version=5, travel=2):
    lumps = [b""] * 14
    lumps[2] = struct.pack("<4fi", 1, 0, 0, 80, 0)
    lumps[7] = b"".join(struct.pack("<3i9f", i, 0, 0, *([0.] * 9)) for i in range(3))
    # Front x>=80 is area 1 and can reach area 2; no return edge.
    lumps[8] = b"".join(struct.pack("<7i", 0, 1, 2, 0, 0, n, first) for n, first in ((0, 0), (1, 0), (0, 1)))
    lumps[9] = struct.pack("<3i6fiH2x", 2, 0, 0, 100, 0, 24, 0, 0, 24, travel, 10)
    lumps[10] = struct.pack("<6i", 0, 0, 0, 0, -1, -2)
    return lump_file(b"EAAS", version, lumps, aas=True)


def fake_record(name, author, geometry, base="", value=10):
    return {"id": name, "map": name, "authors": [author] if author else [], "geometry_sha256": geometry,
            "metadata": {"map_base": base}, "modes": ["duel"], "players_max": 4,
            "analysis": {"metrics": {"weapon_count": value}}}


class BinaryTests(unittest.TestCase):
    def test_trace_hits_thin_wall_in_both_directions_and_inside(self):
        bsp = Bsp(fixture_bsp())
        self.assertAlmostEqual(bsp.trace((0, 0, 46), (160, 0, 46)), .4)
        self.assertAlmostEqual(bsp.trace((160, 0, 46), (0, 0, 46)), .5)
        self.assertEqual(bsp.trace((72, 0, 46), (72, 0, 46)), 0)
        self.assertEqual(bsp.trace((0, 200, 46), (160, 200, 46)), 1)

    def test_occluder_changes_exposure_without_changing_items(self):
        closed, _ = analyze(Bsp(fixture_bsp()))
        opened, _ = analyze(Bsp(fixture_bsp(wall=False)))
        self.assertEqual(closed["metrics"]["spawn_pair_world_los_fraction"], 0)
        self.assertEqual(opened["metrics"]["spawn_pair_world_los_fraction"], 1)
        self.assertEqual(closed["classes"], opened["classes"])

    def test_dm_filter_and_vertical_anchors(self):
        extra = (b'{"classname" "weapon_bfg" "origin" "0 0 900" "spawnflags" "2048"}'
                 b'{"classname" "item_armor_body" "origin" "0 0 280"}')
        analysis, _ = analyze(Bsp(fixture_bsp(extra=extra)))
        self.assertNotIn("weapon_bfg", analysis["classes"])
        self.assertEqual(analysis["excluded_not_deathmatch"], {"weapon_bfg": 1})
        self.assertGreater(analysis["metrics"]["combat_height_p90_p10"], 0)

    def test_unsupported_pickups_are_explicit_and_not_silently_admitted(self):
        extra = (b'{"classname" "weapon_grenadelaucnher" "origin" "0 0 24"}'
                 b'{"classname" "weapon_mod" "origin" "0 0 24" "spawnflags" "2048"}')
        analysis, _ = analyze(Bsp(fixture_bsp(extra=extra)))
        self.assertEqual(analysis["unsupported_pickup_classes"], {"weapon_grenadelaucnher": 1})
        record = fake_record("mod", "Author", "geometry")
        record.update(partition="reference", split_group="group", profile_eligible=False)
        self.assertEqual(build_profile([record], "hash")["cohorts"], {})

    def test_entity_change_preserves_geometry_identity(self):
        a, b = Bsp(fixture_bsp()), Bsp(fixture_bsp(extra=b'{"classname" "light" "origin" "0 0 100"}'))
        self.assertNotEqual(a.sha256, b.sha256)
        self.assertEqual(a.geometry_sha256, b.geometry_sha256)

    def test_fullbright_zero_offsets_are_unavailable_not_sample_reads(self):
        data = bytearray(fixture_bsp())
        face_offset = struct.unpack_from("<i", data, 8 + 6 * 8)[0]
        struct.pack_into("<i", data, face_offset + 16, 0)
        bsp = Bsp(bytes(data))
        self.assertTrue(bsp.file_issues)
        struct.pack_into("<i", data, face_offset + 16, 1)
        with self.assertRaises(FormatError):
            Bsp(bytes(data))

    def test_repeated_entity_keys_use_native_last_value_and_record_issue(self):
        issues = []
        parsed = entities(b'{"classname" "worldspawn" "message" "first" "message" "last"}', issues)
        self.assertEqual(parsed[0]["message"], "last")
        self.assertEqual(len(issues), 1)

    def test_reject_corrupt_binary_envelopes(self):
        original = fixture_bsp()
        mutations = []
        wrong_version = bytearray(original)
        struct.pack_into("<i", wrong_version, 4, 46)
        mutations.append(wrong_version)
        outside = bytearray(original)
        struct.pack_into("<i", outside, 8, len(original) + 20)
        mutations.append(outside)
        overlap = bytearray(original)
        struct.pack_into("<i", overlap, 16, 160)
        mutations.append(overlap)
        cycle = bytearray(original)
        node_offset = struct.unpack_from("<i", cycle, 8 + 4 * 8)[0]
        struct.pack_into("<i", cycle, node_offset + 4, 0)
        mutations.append(cycle)
        invalid_float = bytearray(original)
        plane_offset = struct.unpack_from("<i", invalid_float, 16)[0]
        struct.pack_into("<f", invalid_float, plane_offset, math.nan)
        mutations.append(invalid_float)
        invalid_vertex = bytearray(original)
        edge_offset = struct.unpack_from("<i", invalid_vertex, 8 + 11 * 8)[0]
        struct.pack_into("<H", invalid_vertex, edge_offset + 4, 4000)
        mutations.append(invalid_vertex)
        for data in mutations + [original[:100], original[:-1]]:
            with self.subTest(length=len(data)), self.assertRaises(FormatError):
                Bsp(bytes(data))

    def test_entity_parser_does_not_salvage_malformed_text(self):
        good = b'// comment\n {"classname" "worldspawn" "message" "a \\"quote\\""}'
        self.assertEqual(entities(good)[0]["classname"], "worldspawn")
        for bad in (b'{"classname" "worldspawn"', b'{"classname" "worldspawn" "broken" "}',
                    b'{"classname" "worldspawn" "classname" "light"}'):
            with self.assertRaises(FormatError):
                entities(bad)

    def test_aas_versions_and_directed_routes(self):
        for version in (4, 5):
            aas = Aas(fixture_aas(version))
            self.assertEqual(aas.area_at((100, 0, 24)), 1)
            self.assertEqual(aas.area_at((0, 0, 24)), 2)
            self.assertEqual(aas.graph()[1], {2})
            self.assertEqual(aas.graph()[2], set())
            self.assertEqual(aas.bsp_checksum, 0)
        self.assertEqual(Aas(fixture_aas(travel=12)).graph()[1], set())

    def test_scc_is_directional_and_navigation_remains_unverified(self):
        graph = [set(), {2}, {1, 3}, set()]
        active, membership, sizes = strongly_connected(graph)
        self.assertEqual(sorted(sizes), [1, 2])
        self.assertEqual(membership[1], membership[2])
        self.assertNotEqual(membership[1], membership[3])
        analysis, _ = analyze(Bsp(fixture_bsp()))
        nav = navigation(Aas(fixture_aas()), analysis["anchors"])
        self.assertFalse(nav["bsp_association_verified"])
        self.assertEqual(nav["status"], "unverified_sidecar")
        self.assertEqual(nav["reachable_bound_pair_fraction"], .5)


class CorpusTests(unittest.TestCase):
    def test_tolerant_metadata_flags_bad_line_without_swallowing_next(self):
        fields, issues = metadata(b'{\n"map_name" "Example"\n"score_level" "\n"players_max" "8"\n}')
        self.assertEqual(fields["players_max"], "8")
        self.assertEqual(len(issues), 1)

    def test_author_family_and_duplicates_share_splits(self):
        records = [fake_record("a", "Author A", "1"), fake_record("b", "Author A", "2"),
                   fake_record("c", "Author C", "2"), fake_record("d", "Author D", "4", base="c")]
        assign_splits(records, {"lessons": [{"maps": ["a"], "partition": "test"}]})
        self.assertEqual({r["partition"] for r in records}, {"test"})
        self.assertEqual(len({r["split_group"] for r in records}), 1)
        with self.assertRaises(ValueError):
            assign_splits(records, {"lessons": [{"maps": ["a"], "partition": "test"},
                                                {"maps": ["d"], "partition": "reference"}]})

    def test_missing_curriculum_map_and_unknown_author(self):
        records = [fake_record("a", "", "1")]
        assign_splits(records, {"lessons": [{"maps": ["a"]}]})
        self.assertEqual(records[0]["partition"], "study_only")
        with self.assertRaises(ValueError):
            assign_splits(records, {"lessons": [{"maps": ["missing"]}]})

    def test_reference_profile_excludes_holdouts_and_weights_groups(self):
        records = [fake_record(str(i), str(i), str(i), value=i) for i in range(6)]
        for i, record in enumerate(records):
            record.update(partition="reference", split_group=str(i))
        repeat = copy.deepcopy(records[0])
        repeat["id"] = "repeat"
        records.extend([copy.deepcopy(repeat) for _ in range(20)])
        heldout = fake_record("test", "Test", "test", value=999999)
        heldout.update(partition="test", split_group="test")
        records.append(heldout)
        profile = build_profile(records, "hash")
        stats = profile["cohorts"]["duel"]["metrics"]["weapon_count"]
        self.assertEqual(stats["count"], 6)
        self.assertEqual(stats["p50"], 2.5)
        self.assertEqual(stats["max"], 5)
        self.assertNotIn("test", profile["cohorts"]["duel"]["reference_ids"])
        comparison = compare_metrics(profile, "duel", {"metrics": {"weapon_count": 100}})
        self.assertEqual(comparison["comparisons"][0]["observation"], "above_reference_band")
        with self.assertRaises(ValueError):
            compare_metrics(profile, "duel", {"metrics": {}, "unsupported_pickup_classes": {"weapon_mod": 1}})
        profile["extractor_version"] = "different"
        with self.assertRaises(ValueError):
            compare_metrics(profile, "duel", {"metrics": {}})

    def test_ingest_roundtrip_determinism_and_html_escaping(self):
        base = PROJECT / ".agents/tmp/map-study-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            temporary = Path(directory).resolve()
            self.assertTrue(temporary.is_relative_to(base.resolve()))
            source = temporary / "archive/test/pack/maps"
            source.mkdir(parents=True)
            (source / "test.bsp").write_bytes(fixture_bsp())
            (source / "ref").mkdir()
            (source / "ref/test.ref").write_text('"original_author" "Project test"\n"gametype" "duel"\n')
            curriculum = temporary / "curriculum.json"
            curriculum.write_text(json.dumps({"lessons": [{"id": "test", "title": "Test", "maps": ["test"],
                                                           "question": "Observe", "rules": []}]}))
            output = temporary / "output"
            command = [sys.executable, "-B", "-m", "tools.map_study", "ingest", "--archive", str(temporary / "archive"),
                       "--output", str(output), "--curriculum", str(curriculum), "--strict"]
            for run in range(2):
                result = subprocess.run(command, cwd=PROJECT, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                hashes = {p.relative_to(output).as_posix(): sha256(p.read_bytes()) for p in output.rglob("*") if p.is_file()}
                if run:
                    self.assertEqual(hashes, first_hashes)
                first_hashes = hashes
            html = next((output / "maps").glob("*.html")).read_text(encoding="utf-8")
            self.assertIn("Test &lt;map&gt;", html)
            self.assertNotIn("Test <map>", html)
            # Explicit cleanup preconditions: only this test's directory, with no links.
            self.assertTrue(temporary.is_relative_to(base.resolve()))
            self.assertFalse(any(is_link(p) for p in temporary.rglob("*")))


if __name__ == "__main__":
    unittest.main()
