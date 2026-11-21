#!/usr/bin/env python3
"""Verify a manually closed Pebble release ticket against the App Store."""

import argparse
import json
import os
from pathlib import Path
import re
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


def parse_ticket_title(title):
    match = re.fullmatch(r"Publish PushTo Tool v(\d+\.\d+\.\d+(?:[.+-][A-Za-z0-9.+-]+)?)", title)
    return match.group(1) if match else None


def request_json(url, headers=None):
    request = Request(url, headers=headers or {})
    try:
        with urlopen(request, timeout=30) as response:
            return response.status, json.loads(response.read())
    except HTTPError as error:
        try:
            payload = json.loads(error.read())
        except (json.JSONDecodeError, UnicodeDecodeError):
            payload = {}
        return error.code, payload


def verify_appstore(request_url, request_token, api_base, app_uuid, version,
                   request_fn=request_json):
    oidc_url = f"{request_url}&{urlencode({'audience': api_base})}"
    status, payload = request_fn(
        oidc_url, {"Authorization": f"bearer {request_token}"}
    )
    if status == 401:
        return {"status": "failed", "reason": "GitHub OIDC authentication returned HTTP 401."}
    if status != 200 or not isinstance(payload, dict) or not isinstance(payload.get("value"), str):
        return {"status": "failed", "reason": f"GitHub OIDC authentication returned HTTP {status}."}

    token = payload["value"]
    status, payload = request_fn(
        f"{api_base.rstrip('/')}/api/v1/developer/me",
        {"Authorization": "Bearer " + token},
    )
    if status == 401:
        return {"status": "failed", "reason": "Pebble App Store authentication returned HTTP 401."}
    if status != 200 or not isinstance(payload, dict) or not isinstance(payload.get("apps"), list):
        return {"status": "failed", "reason": f"Pebble App Store lookup returned HTTP {status}."}

    matches = [
        app for app in payload["apps"]
        if isinstance(app, dict)
        and isinstance(app.get("app_uuid"), str)
        and app["app_uuid"].lower() == app_uuid.lower()
    ]
    if len(matches) != 1:
        return {"status": "failed", "reason": "Expected exactly one App Store listing for the app UUID."}
    versions = matches[0].get("versions")
    if isinstance(versions, list) and version in versions:
        return {"status": "published", "reason": f"Pebble App Store reports v{version} as published."}
    return {"status": "failed", "reason": f"Pebble App Store does not list v{version} as published."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--app-uuid", required=True)
    parser.add_argument("--api-base", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()

    result = verify_appstore(
        os.environ["ACTIONS_ID_TOKEN_REQUEST_URL"],
        os.environ["ACTIONS_ID_TOKEN_REQUEST_TOKEN"],
        args.api_base,
        args.app_uuid,
        args.version,
    )
    Path(args.report).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(result["reason"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
