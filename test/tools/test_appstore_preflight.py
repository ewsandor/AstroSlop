import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.appstore_preflight import check_appstore, main


API_BASE = "https://appstore-api.repebble.com"
APP_UUID = "0f463d47-47e7-46f1-a7d6-94590ffc001d"


class AppStorePreflightTests(unittest.TestCase):
    def run_check(self, responses):
        requests = []

        def request_fn(url, headers):
            requests.append((url, headers))
            return responses.pop(0)

        result = check_appstore(
            "https://token.actions.githubusercontent.com/request",
            "request-token",
            API_BASE,
            APP_UUID,
            request_fn,
        )
        return result, requests

    def test_oidc_401_is_reported_as_soft_failure(self):
        result, requests = self.run_check([(401, b"unauthorized")])
        self.assertEqual(
            result, {"status": "unauthorized", "endpoint": "github_oidc", "http_status": 401}
        )
        self.assertEqual(len(requests), 1)

    def test_cli_writes_401_report_and_exits_successfully(self):
        result = {"status": "unauthorized", "endpoint": "appstore_api", "http_status": 401}
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "preflight.json"
            environment = {
                "APPSTORE_PREFLIGHT_REPORT": str(report_path),
                "ACTIONS_ID_TOKEN_REQUEST_URL": "https://token/request",
                "ACTIONS_ID_TOKEN_REQUEST_TOKEN": "request-token",
                "APPSTORE_API_BASE": API_BASE,
                "APP_UUID": APP_UUID,
            }
            with patch.dict(os.environ, environment), patch(
                "tools.appstore_preflight.check_appstore", return_value=result
            ):
                self.assertEqual(main(), 0)
            self.assertEqual(json.loads(report_path.read_text()), result)

    def test_appstore_401_is_reported_as_soft_failure(self):
        result, _ = self.run_check([
            (200, b'{"value":"id-token"}'),
            (401, b"unauthorized"),
        ])
        self.assertEqual(
            result, {"status": "unauthorized", "endpoint": "appstore_api", "http_status": 401}
        )

    def test_other_http_errors_remain_failures(self):
        for status in (403, 500):
            with self.subTest(status=status), self.assertRaisesRegex(RuntimeError, str(status)):
                self.run_check([(status, b"error")])
            with self.subTest(endpoint="appstore", status=status), self.assertRaisesRegex(
                RuntimeError, str(status)
            ):
                self.run_check([(200, b'{"value":"id-token"}'), (status, b"error")])

    def test_authenticated_listing_is_validated(self):
        result, requests = self.run_check([
            (200, b'{"value":"id-token"}'),
            (200, json.dumps({"apps": [{"app_uuid": APP_UUID}]}).encode()),
        ])
        self.assertEqual(result["status"], "authorized")
        self.assertEqual(requests[1][1]["Authorization"], "Bearer " + "id-token")


if __name__ == "__main__":
    unittest.main()
