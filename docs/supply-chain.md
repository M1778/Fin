# Supply chain — what a `finc` release carries

Every release built by [`.github/workflows/release.yml`](../.github/workflows/release.yml)
ships a CycloneDX SBOM per target beside the archive, and the whole published set is covered
by SLSA build provenance. Both come from the run that makes the bytes.

## What is produced

| Artifact | Name | What it answers |
| --- | --- | --- |
| Archive / native installable | `finc-<version>-<target>.{tar.gz,zip,deb,rpm,dmg}`, `-setup.exe` | the compiler |
| Checksum sidecar | `<asset>.sha256` | did this download arrive intact |
| Version index | `index.json` | the authoritative sums `finn` reads |
| SBOM | `finc-<version>-<target>.cdx.json` | which dependencies are in this build |
| Provenance | attestation, not a file | which workflow and tag built it |

The SBOM lists what the build resolved: `fmt` (driver) and `gtest` (test suite), read from the
`conan.lock` that `conan install` wrote, plus `llvm` from the compiling toolchain, with
`finc:build:target`, `finc:build:type`, `finc:build:llvm-version` and `finc:build:llvm-source`
properties.

## Verify a download

```sh
V=1.0.0; T=x86_64-unknown-linux-gnu; A=finc-$V-$T.tar.gz
gh release download "v$V" --repo M1778/Fin \
  --pattern "$A" --pattern "$A.sha256" --pattern "finc-$V-$T.cdx.json"

sha256sum -c "$A.sha256"; echo $?          # macOS: shasum -a 256 -c "$A.sha256"
gh attestation verify "$A" --repo M1778/Fin \
  --signer-workflow M1778/Fin/.github/workflows/release.yml --source-ref "refs/tags/v$V"
```

`--signer-workflow` is the part that matters: `--repo` alone accepts a valid attestation from
any workflow in the repository; naming the workflow and tag pins it to the release pipeline at
the commit you asked for. Add `--format json` to read the claims rather than a verdict.

### Check the SBOM

```sh
jq -r '.components[] | "\(.name) \(.version)"' "finc-$V-$T.cdx.json"
jq -r '.metadata.properties[] | select(.name | startswith("finc:build:"))
       | "\(.name)=\(.value)"' "finc-$V-$T.cdx.json"
```

`index.json` carries only the portable archives — `finn` resolves
`versions[<v>].targets[<triple>]` from it, so a second entry shape would be a contract
change; fetch the SBOM by name as above.

### Verify offline

```sh
gh attestation download "$A" --repo M1778/Fin   # writes sha256-<digest>.jsonl
gh attestation verify "$A" --repo M1778/Fin \
  --bundle "sha256-$(sha256sum "$A" | cut -d' ' -f1).jsonl"
```

## What each control addresses

| Control | Threat |
| --- | --- |
| `.sha256` sidecar, `index.json`, `sha256sum -c` | Corrupted or truncated download; a mirror serving different bytes. Does **not** prove who built it — a sidecar travelling beside the file can be replaced with it. |
| Build provenance (Sigstore, keyless) | "Was this built from `M1778/Fin` at tag `vX.Y.Z` by the release workflow, or assembled by someone with upload rights?" The signature is bound to the workflow identity, so a compromised token that uploads a file does not get a matching attestation. |
| SBOM | "Which dependency versions are in the binary I am about to trust with my source?" Makes `fmt`/`gtest`/LLVM CVE triage possible and a substituted dependency visible. |
| Annotated-tag check in `preflight` | A lightweight tag has no object to sign; a tag run now fails on one. |
| Exact 20-asset allow-list | A stray or substituted asset reaching the release page; `finn` downloads by name, so an unexpected name is a name it may pick. |

## Not attested yet

- **Nothing is signed with a maintainer key.** A *lightweight* tag fails the check; an *unsigned
  annotated* tag is only reported, because hosted runners hold no maintainer key and requiring
  `git tag -v` would fail every release for something CI cannot satisfy.
- **No cosign `sign-blob` signatures.** Provenance already binds each published digest to the
  workflow identity via Sigstore, so `.sig`/`.pem` would duplicate that guarantee while adding
  two allow-listed assets and an unexercised failure mode.
- **Windows and macOS binaries are not notarized or Authenticode-signed** by this pipeline. The
  SBOM covers declared dependencies, not the C++ sources they compiled: syft reads the Conan
  lockfile, not vendored code or the system libc, and `lib/std/` is part of the archive rather
  than a separate package. Wider surface: [`docs/threat-model.md`](threat-model.md).
