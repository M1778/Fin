#!/usr/bin/env python3
"""Generate the finc version index from the archives a release job just built.

ADR 0010 publishes every sha256 twice: once in an `<asset>.sha256` sidecar and
once here.  This file is the authoritative one, so the sums in it are computed
from the archive bytes rather than copied out of the sidecars -- and the sidecar
is then checked against them, because two published values that can disagree are
worse than one.

`finn` reads this at runtime.  It knows its own target triple at build time, so
the lookup it performs is versions[<version>].targets[<triple>], and the answer
carries the URL to fetch and the sum to check it against.  That is the whole
contract; adding a field is safe, renaming one is not.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import sys

SCHEMA = 1
SUFFIXES = (".tar.gz", ".zip")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def target_of(name, version):
    """finc-<version>-<target-triple><suffix> -> <target-triple>."""
    prefix = "finc-%s-" % version
    if not name.startswith(prefix):
        return None
    for suffix in SUFFIXES:
        if name.endswith(suffix):
            return name[len(prefix):-len(suffix)]
    return None


def semver_key(version):
    core = version.split("-", 1)[0]
    parts = [int(p) if p.isdigit() else 0 for p in core.split(".")]
    while len(parts) < 3:
        parts.append(0)
    # A prerelease sorts below the release it precedes.
    return (parts[0], parts[1], parts[2], 0 if "-" in version else 1)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--prerelease", default="false")
    parser.add_argument("--base-url", required=True,
                        help="URL the assets of this release are served from")
    parser.add_argument("--dist", required=True,
                        help="directory holding the archives and their sidecars")
    parser.add_argument("--previous", default=None,
                        help="index.json from the previous release, if any")
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)

    prerelease = args.prerelease.lower() == "true"

    index = {"schema": SCHEMA, "latest": None, "versions": {}}
    if args.previous and os.path.exists(args.previous):
        with open(args.previous) as handle:
            previous = json.load(handle)
        if previous.get("schema") != SCHEMA:
            sys.exit("previous index has schema %r, this script writes %r; "
                     "migrate it deliberately rather than silently"
                     % (previous.get("schema"), SCHEMA))
        index["latest"] = previous.get("latest")
        index["versions"] = previous.get("versions") or {}

    targets = {}
    for name in sorted(os.listdir(args.dist)):
        if name.endswith(".sha256") or not name.endswith(SUFFIXES):
            continue
        target = target_of(name, args.version)
        if target is None:
            sys.exit("%s is not named finc-%s-<target-triple>.{tar.gz,zip}"
                     % (name, args.version))
        path = os.path.join(args.dist, name)
        digest = sha256_of(path)

        sidecar = path + ".sha256"
        if not os.path.exists(sidecar):
            sys.exit("%s has no .sha256 sidecar" % name)
        with open(sidecar) as handle:
            claimed = handle.read().split()[0].lower()
        if claimed != digest:
            sys.exit("%s: sidecar says %s, the bytes say %s"
                     % (name, claimed, digest))

        targets[target] = {
            "file": name,
            "url": "%s/%s" % (args.base_url.rstrip("/"), name),
            "size": os.path.getsize(path),
            "sha256": digest,
        }

    if not targets:
        sys.exit("no archives found in %s" % args.dist)

    index["versions"][args.version] = {
        "tag": args.tag,
        "prerelease": prerelease,
        "released": datetime.datetime.now(datetime.timezone.utc)
                            .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "targets": dict(sorted(targets.items())),
    }

    # `latest` means "what a user who asked for no version gets", so a
    # prerelease never becomes it.
    if not prerelease:
        current = index.get("latest")
        if current is None or semver_key(args.version) >= semver_key(current):
            index["latest"] = args.version
    elif index.get("latest") is None:
        index["latest"] = None

    index["versions"] = dict(sorted(index["versions"].items(),
                                    key=lambda kv: semver_key(kv[0]),
                                    reverse=True))
    index["generated"] = datetime.datetime.now(datetime.timezone.utc) \
                                 .strftime("%Y-%m-%dT%H:%M:%SZ")

    ordered = {
        "schema": index["schema"],
        "generated": index["generated"],
        "latest": index["latest"],
        "versions": index["versions"],
    }
    with open(args.output, "w") as handle:
        json.dump(ordered, handle, indent=2)
        handle.write("\n")
    print("wrote %s: %d version(s), %d target(s) for %s"
          % (args.output, len(ordered["versions"]), len(targets), args.version))


if __name__ == "__main__":
    main()
