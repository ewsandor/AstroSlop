import unittest

from tools.manual_publish_completion import parse_ticket_title, verify_appstore


class ManualPublishCompletionTests(unittest.TestCase):
    def test_exact_ticket_title(self):
        self.assertEqual(
            parse_ticket_title("Publish PushTo Tool v0.4.0"), "0.4.0"
        )
        self.assertIsNone(parse_ticket_title("Other Publish PushTo Tool v0.4.0"))
        self.assertIsNone(parse_ticket_title("Publish PushTo Tool"))

    def test_published_version_is_success(self):
        def request(url, headers):
            if "audience" in url:
                return 200, {"value": "id-token"}
            return 200, {"apps": [{
                "app_uuid": "0f463d47-47e7-46f1-a7d6-94590ffc001d",
                "versions": ["0.4.0"],
            }]}

        result = verify_appstore(
            "https://oidc.example/token?x=1", "request-token",
            "https://appstore.example", "0f463d47-47e7-46f1-a7d6-94590ffc001d",
            "0.4.0", request,
        )
        self.assertEqual(result["status"], "published")

    def test_missing_version_is_failure(self):
        def request(url, headers):
            if "audience" in url:
                return 200, {"value": "id-token"}
            return 200, {"apps": [{
                "app_uuid": "0f463d47-47e7-46f1-a7d6-94590ffc001d",
                "versions": ["0.3.2"],
            }]}

        result = verify_appstore(
            "https://oidc.example/token?x=1", "request-token",
            "https://appstore.example", "0f463d47-47e7-46f1-a7d6-94590ffc001d",
            "0.4.0", request,
        )
        self.assertEqual(result["status"], "failed")
        self.assertIn("does not list", result["reason"])

    def test_401_is_failure(self):
        result = verify_appstore(
            "https://oidc.example/token?x=1", "request-token",
            "https://appstore.example", "0f463d47-47e7-46f1-a7d6-94590ffc001d",
            "0.4.0", lambda url, headers: (401, {}),
        )
        self.assertEqual(result["status"], "failed")
        self.assertIn("401", result["reason"])


if __name__ == "__main__":
    unittest.main()
