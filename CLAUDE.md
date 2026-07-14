# gattn -- collaboration notes

## Versioning

gattn follows [SemVer](https://semver.org/).

- **While `0.x.y`** (pre-stable):
  - Any release containing at least one new feature bumps MINOR:
    `0.1.6 -> 0.2.0` (resets PATCH to 0).
  - Bugfix-only releases bump PATCH: `0.2.0 -> 0.2.1`.
- **From `1.0.0` onward**: standard SemVer. MAJOR for breaking changes,
  MINOR for backward-compatible features, PATCH for backward-compatible
  fixes.

Before proposing a version bump, review the commits since the previous
tag. If any conventional-commit prefix is `feat:`, bump MINOR. Only bump
PATCH when every change is `fix:` / `docs:` / `chore:` / `ci:` / `test:`.

The version string lives in three files that must move together:

- `meson.build`
- `packaging/gattn.spec`
- `packaging/debian/changelog` (add a new top entry, don't rewrite old ones)
