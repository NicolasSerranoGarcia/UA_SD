import logging
import sqlite3
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
from pathlib import Path

from wm_registry.app import create_app
from wm_registry.store import reset_enrollment, verify_station


def bearer(secret):
    return {"Authorization": f"Bearer {secret}"}


class RegistryTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.db = Path(self.directory.name) / "stations.db"
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute("CREATE TABLE stations (id INTEGER PRIMARY KEY, location TEXT, status TEXT)")
            db.execute("INSERT INTO stations VALUES (1, 'legacy', 'Disconnected')")
        self.app = create_app(self.db)
        self.path = "/api/registry/stations/1"

    def register(self, client):
        return client.put(self.path, json={"city": "alicante"})

    def test_enrollment_uses_only_the_registry_table_and_survives_restart(self):
        client = self.app.test_client()
        self.assertEqual(client.get(self.path).status_code, 401)
        with self.assertLogs("wm_registry.audit", logging.INFO) as audit:
            response = self.register(client)
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.headers["Cache-Control"], "no-store")
        token = response.json["credential"]
        self.assertEqual(len(token), 43)
        self.assertNotIn(token, audit.output[0])
        with closing(sqlite3.connect(self.db)) as db, db:
            self.assertEqual(db.execute("SELECT * FROM stations").fetchall(), [(1, "legacy", "Disconnected")])
            digest, = db.execute(
                "SELECT credential_digest FROM registry_enrollments"
            ).fetchone()
            self.assertNotEqual(digest, token.encode())
        self.assertEqual(verify_station(self.db, 1, token), "alicante")
        restarted = create_app(self.db).test_client()
        self.assertEqual(restarted.get(self.path, headers=bearer(token)).json,
                         {"station_id": 1, "city": "alicante"})
        conflict = self.register(restarted)
        self.assertEqual(conflict.status_code, 409)
        self.assertNotIn("credential", conflict.json)

    def test_parallel_attempts_register_the_id_once(self):
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: self.register(self.app.test_client()).status_code, range(2)))
        self.assertEqual(sorted(results), [201, 409])

    def test_credentials_are_station_bound_and_delete_revokes_access(self):
        client = self.app.test_client()
        token = self.register(client).json["credential"]
        self.assertEqual(client.delete(self.path).status_code, 401)
        for method in ("GET", "DELETE"):
            self.assertEqual(client.open("/api/registry/stations/2", method=method,
                                         headers=bearer(token)).status_code, 401)
        self.assertEqual(client.delete(self.path, headers=bearer(token)).status_code, 204)
        self.assertIsNone(verify_station(self.db, 1, token))
        self.assertEqual(client.get(self.path, headers=bearer(token)).status_code, 401)

    def test_replacement_is_explicit_and_old_credential_stops_working(self):
        client = self.app.test_client()
        token = self.register(client).json["credential"]
        self.assertEqual(self.register(client).status_code, 409)
        self.assertTrue(reset_enrollment(self.db, 1))
        self.assertIsNone(verify_station(self.db, 1, token))
        self.assertEqual(self.register(client).status_code, 201)

    def test_bad_city_does_not_claim_the_id(self):
        client = self.app.test_client()
        bad = client.put(self.path, json={"city": "Alicante Spain"})
        self.assertEqual(bad.status_code, 400)
        self.assertEqual(self.register(client).status_code, 201)

    def test_previously_provisioned_id_can_register_without_a_code(self):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute("ALTER TABLE registry_enrollments ADD COLUMN enrollment_digest BLOB")
            db.execute("ALTER TABLE registry_enrollments ADD COLUMN expires_at INTEGER")
            db.execute("INSERT INTO registry_enrollments (station_id, enrollment_digest, expires_at) VALUES (1, X'01', 1)")
        self.assertEqual(self.register(self.app.test_client()).status_code, 201)

    def test_busy_database_returns_503_with_no_secrets(self):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute("BEGIN IMMEDIATE")
            response = self.register(self.app.test_client())
        self.assertEqual(response.status_code, 503)
        self.assertNotIn("credential", response.json)


if __name__ == "__main__":
    unittest.main()
