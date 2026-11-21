#!/usr/bin/env python3
"""Check App Store OIDC access without publishing."""

import json
import os
from pathlib import Path
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit, urlunsplit, parse_qsl
from urllib.request import Request, urlopen


def _request(url, headers):
    request = Request(url, headers=headers)
    try:
        with urlopen(request, timeout=30) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()
    except URLError as error:
        raise RuntimeError(f"HTTP request failed ({type(error.reason).__name__})") from None


def _with_audience(url, audience):
    parsed = urlsplit(url)
    query = parse_qsl(parsed.query, keep_blank_values=True)
    query.append(("audience", audience))
    return urlunsplit(parsed._replace(query=urlencode(query)))


def check_appstore(request_url, request_token, api_base, app_uuid, request_fn=_request, mask_token=None):
    token_url = _with_audience(request_url, api_base)
    status, body = request_fn(token_url, {"Authorization": f"bearer {request_token}"})
    if status == 401:
        return {"status": "unauthorized", "endpoint": "github_oidc", "http_status": status}
    if status != 200:
        raise RuntimeError(f"GitHub OIDC token request returned HTTP {status}")
    try:
        token = json.loads(body)["value"]
    except (ValueError, KeyError, TypeError):
        raise RuntimeError("GitHub OIDC response did not contain a token") from None
    if not isinstance(token, str) or not token:
        raise RuntimeError("GitHub OIDC response did not contain a token")
    if mask_token:
        mask_token(token)

    status, body = request_fn(
        f"{api_base.rstrip('/')}/api/v1/developer/me",
        {"Authorization": "Bearer " + token},
    )
    if status == 401:
        return {"status": "unauthorized", "endpoint": "appstore_api", "http_status": status}
    if status != 200:
        raise RuntimeError(f"App Store developer API returned HTTP {status}")
    try:
        apps = json.loads(body)["apps"]
    except (ValueError, KeyError, TypeError):
        raise RuntimeError("App Store response did not contain an app list") from None
    matches = [
        app for app in apps
        if isinstance(app, dict)
        and isinstance(app.get("app_uuid"), str)
        and app["app_uuid"].lower() == app_uuid.lower()
    ]
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one App Store listing for the app UUID")
    return {"status": "authorized", "endpoint": "appstore_api", "http_status": status}


def main():
    report_path = Path(os.environ.get("APPSTORE_PREFLIGHT_REPORT", "appstore-preflight.json"))
    try:
        result = check_appstore(
            os.environ["ACTIONS_ID_TOKEN_REQUEST_URL"],
            os.environ["ACTIONS_ID_TOKEN_REQUEST_TOKEN"],
            os.environ["APPSTORE_API_BASE"],
            os.environ["APP_UUID"],
            mask_token=lambda token: print(f"::add-mask::{token}"),
        )
    except Exception as error:
        result = {"status": "error", "error": str(error)}
        report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"::error::{error}")
        return 1

    report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if result["status"] == "unauthorized":
        print(f"::warning::App Store preflight returned HTTP 401 from {result['endpoint']}; manual publishing is required.")
    else:
        print("App Store preflight succeeded; the release will still require manual publishing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
