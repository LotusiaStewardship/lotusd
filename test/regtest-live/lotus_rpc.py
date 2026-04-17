#!/usr/bin/env python3
"""Shared JSON-RPC client helper for Lotus regtest miners."""

import base64
import json
import time
import urllib.request
import urllib.error


class LotusRPC:
    def __init__(self, url="http://lotusd-node1:12604", user="test", password="test"):
        self.url = url
        self.user = user
        self.password = password
        self._id = 0

    def call(self, method, params=None):
        self._id += 1
        payload = json.dumps({
            "jsonrpc": "2.0",
            "id": self._id,
            "method": method,
            "params": params or [],
        }).encode()

        req = urllib.request.Request(self.url, data=payload)
        req.add_header("Content-Type", "application/json")

        credentials = base64.b64encode(
            f"{self.user}:{self.password}".encode()
        ).decode()
        req.add_header("Authorization", f"Basic {credentials}")

        try:
            resp = urllib.request.urlopen(req, timeout=30)
            body = resp.read().decode()
        except urllib.error.HTTPError as e:
            body = e.read().decode()
            try:
                result = json.loads(body)
                err = result.get("error", {})
                raise RuntimeError(
                    f"RPC error ({method}): {err.get('message', body)}"
                ) from None
            except (json.JSONDecodeError, AttributeError):
                raise RuntimeError(f"HTTP {e.code}: {body}") from None

        result = json.loads(body)
        if result.get("error"):
            raise RuntimeError(
                f"RPC error ({method}): {result['error'].get('message', result['error'])}"
            )
        return result.get("result")

    def wait_ready(self, timeout=120):
        """Block until the node's RPC is responding."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                info = self.call("getblockchaininfo")
                return info
            except Exception:
                time.sleep(0.5)
        raise TimeoutError("Node RPC did not become ready")

    def wait_peers(self, min_peers=1, timeout=60):
        """Block until the node has at least min_peers connections."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                info = self.call("getnetworkinfo")
                if info["connections"] >= min_peers:
                    return info["connections"]
            except Exception:
                pass
            time.sleep(0.5)
        raise TimeoutError(f"Node did not reach {min_peers} peer(s)")
