#!/usr/bin/env python3
"""Best-effort monitor: is the vendored command-gate corpus still in sync?

The accept/reject corpus is vendored (a checked-in copy pinned by SHA-256 in
``generate_gate_vectors.py``). The pin and ``--check`` protect the local
snapshot, but neither notices when the upstream corpus is UPDATED. This script
fetches the upstream copy and compares its SHA-256 to our pin so a scheduled job
can flag divergence for a deliberate review-and-sync.

A mismatch means "review the upstream change", not "the firmware is wrong".

Exit codes:
    0  in sync (upstream bytes == pinned SHA-256)
    1  confirmed drift (upstream fetched, bytes differ)
    2  unable to check (network/HTTP/size failure -- NOT treated as drift)
"""

import hashlib
import http.client
import sys
import time
import urllib.error
import urllib.request

from generate_gate_vectors import EXPECTED_SHA256, SOURCE_PATH, SOURCE_REPO

UPSTREAM_URL = f"https://raw.githubusercontent.com/{SOURCE_REPO}/main/{SOURCE_PATH}"

TIMEOUT_S = 20
MAX_BYTES = 1 << 20  # 1 MiB; the corpus is a few KiB
ATTEMPTS = 3
RETRY_HTTP = {429, 500, 502, 503, 504}


class _FetchError(RuntimeError):
    """Any reason the upstream bytes could not be verified (-> exit 2)."""


class _HttpsOnlyRedirect(urllib.request.HTTPRedirectHandler):
    """Refuse redirects that downgrade off HTTPS."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.lower().startswith("https://"):
            raise urllib.error.URLError(f"refusing non-HTTPS redirect to {newurl}")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


_OPENER = urllib.request.build_opener(_HttpsOnlyRedirect)


def _annotate(level, message):
    """Emit a GitHub Actions annotation plus a human-readable line."""
    print(f"::{level}::{message}")
    print(message)


def _fetch_once():
    """One attempt. Returns bytes, or raises _FetchError (non-retryable) or a
    transport exception (retryable)."""
    if not UPSTREAM_URL.lower().startswith("https://"):
        raise _FetchError(f"refusing non-HTTPS URL {UPSTREAM_URL}")
    req = urllib.request.Request(UPSTREAM_URL, headers={"User-Agent": "oomwoo-corpus-sync"})
    with _OPENER.open(req, timeout=TIMEOUT_S) as resp:
        final = resp.geturl()
        if not final.lower().startswith("https://"):
            raise _FetchError(f"refusing non-HTTPS final URL {final}")
        data = resp.read(MAX_BYTES + 1)
        clen = resp.getheader("Content-Length")
    if len(data) > MAX_BYTES:
        raise _FetchError(f"upstream response exceeds {MAX_BYTES} bytes")
    if clen is not None:
        try:
            declared = int(clen)
        except ValueError as exc:
            raise _FetchError(f"malformed Content-Length {clen!r}") from exc
        if declared != len(data):
            # Truncated/incomplete body: cannot verify, and may be transient.
            raise http.client.IncompleteRead(data, declared - len(data))
    return data


def fetch_upstream():
    """Return upstream bytes, or raise _FetchError if it cannot be checked."""
    last = None
    for attempt in range(1, ATTEMPTS + 1):
        try:
            return _fetch_once()
        except _FetchError:
            raise  # non-retryable (oversize, non-HTTPS, malformed header)
        except urllib.error.HTTPError as exc:
            last = f"HTTP {exc.code}"
            if exc.code not in RETRY_HTTP:
                raise _FetchError(f"unexpected {last} fetching upstream") from exc
        except (OSError, http.client.HTTPException) as exc:
            # URLError, SSLError, timeouts, connection resets, IncompleteRead,
            # BadStatusLine, etc. -- transient; retry then give up.
            last = str(getattr(exc, "reason", exc)) or type(exc).__name__
        if attempt < ATTEMPTS:
            time.sleep(2 ** attempt)  # 2s, 4s backoff
    raise _FetchError(f"fetch failed after {ATTEMPTS} attempts ({last})")


def main():
    try:
        upstream = fetch_upstream()
    except _FetchError as exc:
        _annotate("error", f"corpus sync status unknown: {exc}; URL {UPSTREAM_URL}")
        return 2

    upstream_sha = hashlib.sha256(upstream).hexdigest()
    if upstream_sha == EXPECTED_SHA256:
        print(f"command gate corpus in sync (sha256 {EXPECTED_SHA256})")
        return 0

    _annotate(
        "error",
        "upstream command gate corpus differs from the vendored pin; review the "
        "upstream change and deliberately sync if appropriate.",
    )
    print(f"  upstream : {upstream_sha}")
    print(f"  pinned   : {EXPECTED_SHA256}")
    print(f"  url      : {UPSTREAM_URL}")
    print(
        "To sync (only after reviewing the upstream diff): replace "
        "tests/conformance/command_gate_vectors_v1.json, update EXPECTED_SHA256 "
        "(and EXPECTED_COUNT / SOURCE_REF if changed) in tools/generate_gate_vectors.py, "
        "refresh the provenance note, regenerate the header "
        "(python3 tools/generate_gate_vectors.py), and re-run the conformance test."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
