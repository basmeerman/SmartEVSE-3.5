#!/usr/bin/env python3
"""
check_mongoose_patches.py — assert local patches to the vendored Mongoose survive.

SmartEVSE-3/src/mongoose.c is a vendored amalgamation, not a managed dependency,
so a local fix to it is lost the moment someone drops in a newer mongoose.c. A
comment in the file asks the next person to re-apply the patch; this script is
what actually notices when they don't.

Currently guarded:

  SNI on the non-verifying TLS path
      Upstream Mongoose calls mbedtls_ssl_set_hostname() only when a CA is
      configured, treating the hostname purely as something to verify against.
      But SNI is also how a shared frontend chooses which certificate to
      present. Without it, ocpp.road.io (Cloudflare) answers the ClientHello
      with handshake_failure (alert 40), so every wss:// OCPP backend URL fails
      to connect — silently, because the release build compiles out the logs
      that would say so.

      The invariant: mbedtls_ssl_set_hostname() must be reached regardless of
      whether opts->ca is empty. Concretely, the call must appear BEFORE the
      `if (opts->ca.len == 0 ...)` branch, not inside its else.

Run from the repo root or from test/native:

    python3 scripts/check_mongoose_patches.py
"""

import re
import sys
from pathlib import Path


def find_mongoose() -> Path:
    here = Path(__file__).resolve()
    for base in (here.parents[3], here.parents[4] if len(here.parents) > 4 else here.parents[3]):
        candidate = base / "SmartEVSE-3" / "src" / "mongoose.c"
        if candidate.is_file():
            return candidate
    sys.exit("FAIL: could not locate SmartEVSE-3/src/mongoose.c")


def check_sni(src: str) -> list:
    """SNI must be set on both TLS paths, i.e. before the CA branch."""
    errors = []

    # Narrow to the mbedTLS mg_tls_init(); the builtin/openssl backends in the
    # amalgamation have their own, unpatched, copies of similar-looking code.
    start = src.find("#if MG_TLS == MG_TLS_MBED")
    if start < 0:
        return ["mbedTLS section not found — is MG_TLS_MBED still the backend?"]
    body = src[start:]

    init = re.search(r"void mg_tls_init\(struct mg_connection \*c,.*?\n}", body, re.S)
    if not init:
        return ["mbedTLS mg_tls_init() not found"]
    init_body = init.group(0)

    sni = init_body.find("mbedtls_ssl_set_hostname")
    ca_branch = init_body.find("if (opts->ca.len == 0")

    if sni < 0:
        errors.append("mbedtls_ssl_set_hostname() is gone from mg_tls_init() — "
                      "wss:// backends behind a shared frontend will not connect")
    elif ca_branch < 0:
        errors.append("the opts->ca branch in mg_tls_init() no longer looks as "
                      "expected; re-check the SNI patch by hand")
    elif sni > ca_branch:
        errors.append("mbedtls_ssl_set_hostname() is inside the CA branch again, "
                      "so no SNI is sent when verification is disabled — this is "
                      "the upstream behaviour the local patch exists to change")

    return errors


def main() -> int:
    path = find_mongoose()
    src = path.read_text(encoding="utf-8", errors="replace")

    errors = check_sni(src)

    if errors:
        print(f"FAIL: local patches missing from {path}")
        for e in errors:
            print(f"  - {e}")
        print("\nSee the SMARTEVSE LOCAL PATCH comment in mongoose.c and "
              "docs/upstream-differences.md.")
        return 1

    print(f"OK: local Mongoose patches present in {path.name} (SNI on non-verifying path)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
