//! Package registry system for HEXA-LANG.
//!
//! Manages package manifests (hexa.toml), dependency resolution, lock files,
//! and CLI commands for the HEXA package ecosystem.

use std::collections::HashMap;
use std::fmt;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

// ── Manifest types ──────────────────────────────────────────

/// Parsed hexa.toml manifest.
#[derive(Debug, Clone)]
pub struct Manifest {
    pub package: PackageInfo,
    pub dependencies: Vec<Dependency>,
}

/// [package] section of hexa.toml.
#[derive(Debug, Clone)]
pub struct PackageInfo {
    pub name: String,
    pub version: String,
    pub description: Option<String>,
}

/// A single dependency entry.
#[derive(Debug, Clone)]
pub struct Dependency {
    pub name: String,
    pub source: DepSource,
}

/// Where a dependency comes from.
#[derive(Debug, Clone)]
pub enum DepSource {
    /// Registry dependency: `pkg = "1.0"` or `pkg = "^1.0"`
    Registry { version: String },
    /// Git dependency: `pkg = { version = "1.0", git = "https://..." }`
    Git { version: String, url: String },
    /// Path dependency: `pkg = { path = "../local-lib" }`
    Path { path: String },
}

impl fmt::Display for DepSource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DepSource::Registry { version } => write!(f, "\"{}\"", version),
            DepSource::Git { version, url } => {
                write!(f, "{{ version = \"{}\", git = \"{}\" }}", version, url)
            }
            DepSource::Path { path } => write!(f, "{{ path = \"{}\" }}", path),
        }
    }
}

// ── Lock file types ─────────────────────────────────────────

/// A resolved lock file entry.
#[derive(Debug, Clone)]
pub struct LockEntry {
    pub name: String,
    pub version: String,
    pub source: String, // "registry", "git+<url>#<commit>", "path+<path>"
    pub checksum: Option<String>,
}

/// Complete lock file.
#[derive(Debug, Clone)]
pub struct LockFile {
    pub entries: Vec<LockEntry>,
}

// ── Errors ──────────────────────────────────────────────────

#[derive(Debug)]
pub struct PackageError {
    pub message: String,
}

impl fmt::Display for PackageError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl PackageError {
    pub fn new(msg: impl Into<String>) -> Self {
        Self { message: msg.into() }
    }
}

type PkgResult<T> = Result<T, PackageError>;

// ── Manifest parsing ────────────────────────────────────────

/// Parse a hexa.toml file into a Manifest.
pub fn parse_manifest(content: &str) -> PkgResult<Manifest> {
    let mut name = None;
    let mut version = None;
    let mut description = None;
    let mut dependencies = Vec::new();

    let mut current_section = String::new();

    for line in content.lines() {
        let trimmed = line.trim();

        // Skip blank lines and comments
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        // Section headers
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            current_section = trimmed[1..trimmed.len() - 1].to_string();
            continue;
        }

        // Key = value pairs
        if let Some((key, value)) = parse_kv(trimmed) {
            match current_section.as_str() {
                "package" => match key.as_str() {
                    "name" => name = Some(unquote(&value)),
                    "version" => version = Some(unquote(&value)),
                    "description" => description = Some(unquote(&value)),
                    _ => {} // Ignore unknown keys
                },
                "dependencies" => {
                    let dep = parse_dependency(&key, &value)?;
                    dependencies.push(dep);
                }
                _ => {} // Ignore unknown sections
            }
        }
    }

    let name = name.ok_or_else(|| PackageError::new("missing 'name' in [package]"))?;
    let version = version.unwrap_or_else(|| "0.1.0".to_string());

    Ok(Manifest {
        package: PackageInfo { name, version, description },
        dependencies,
    })
}

/// Parse a dependency value. Handles:
///   `"1.0"` -> Registry
///   `{ version = "1.0", git = "..." }` -> Git
///   `{ path = "..." }` -> Path
fn parse_dependency(name: &str, value: &str) -> PkgResult<Dependency> {
    let v = value.trim();

    // Simple string: registry dependency
    if v.starts_with('"') && v.ends_with('"') {
        return Ok(Dependency {
            name: name.to_string(),
            source: DepSource::Registry {
                version: unquote(v),
            },
        });
    }

    // Inline table: { key = "val", ... }
    if v.starts_with('{') && v.ends_with('}') {
        let inner = &v[1..v.len() - 1];
        let fields = parse_inline_table(inner);

        if let Some(path) = fields.get("path") {
            return Ok(Dependency {
                name: name.to_string(),
                source: DepSource::Path {
                    path: unquote(path),
                },
            });
        }

        if let Some(git) = fields.get("git") {
            let ver = fields
                .get("version")
                .map(|v| unquote(v))
                .unwrap_or_else(|| "latest".to_string());
            return Ok(Dependency {
                name: name.to_string(),
                source: DepSource::Git {
                    version: ver,
                    url: unquote(git),
                },
            });
        }

        if let Some(ver) = fields.get("version") {
            return Ok(Dependency {
                name: name.to_string(),
                source: DepSource::Registry {
                    version: unquote(ver),
                },
            });
        }

        return Err(PackageError::new(format!(
            "dependency '{}': inline table must have 'version', 'git', or 'path'",
            name
        )));
    }

    Err(PackageError::new(format!(
        "dependency '{}': invalid value '{}'",
        name, v
    )))
}

/// Parse `key = value` from a line. Returns None if not a valid kv pair.
fn parse_kv(line: &str) -> Option<(String, String)> {
    let eq_pos = line.find('=')?;
    let key = line[..eq_pos].trim().to_string();
    let value = line[eq_pos + 1..].trim().to_string();
    if key.is_empty() {
        return None;
    }
    Some((key, value))
}

/// Parse an inline TOML table like `version = "1.0", git = "https://..."`
fn parse_inline_table(s: &str) -> HashMap<String, String> {
    let mut map = HashMap::new();
    // Split on commas, but respect quotes
    for part in split_inline_entries(s) {
        let part = part.trim();
        if let Some((k, v)) = parse_kv(part) {
            map.insert(k, v);
        }
    }
    map
}

/// Split inline table entries on commas, respecting quoted strings.
fn split_inline_entries(s: &str) -> Vec<String> {
    let mut entries = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;

    for ch in s.chars() {
        match ch {
            '"' => {
                in_quotes = !in_quotes;
                current.push(ch);
            }
            ',' if !in_quotes => {
                entries.push(current.clone());
                current.clear();
            }
            _ => current.push(ch),
        }
    }
    if !current.trim().is_empty() {
        entries.push(current);
    }
    entries
}

/// Remove surrounding double quotes from a string.
fn unquote(s: &str) -> String {
    let t = s.trim();
    if t.starts_with('"') && t.ends_with('"') && t.len() >= 2 {
        t[1..t.len() - 1].to_string()
    } else {
        t.to_string()
    }
}

// ── Manifest serialization ──────────────────────────────────

/// Serialize a Manifest back to hexa.toml format.
pub fn serialize_manifest(manifest: &Manifest) -> String {
    let mut out = String::new();
    out.push_str("[package]\n");
    out.push_str(&format!("name = \"{}\"\n", manifest.package.name));
    out.push_str(&format!("version = \"{}\"\n", manifest.package.version));
    if let Some(desc) = &manifest.package.description {
        out.push_str(&format!("description = \"{}\"\n", desc));
    }
    out.push_str("\n[dependencies]\n");
    for dep in &manifest.dependencies {
        out.push_str(&format!("{} = {}\n", dep.name, dep.source));
    }
    out
}

// ── Lock file ───────────────────────────────────────────────

impl LockFile {
    pub fn new() -> Self {
        Self { entries: Vec::new() }
    }

    /// Parse a hexa.lock file.
    pub fn parse(content: &str) -> PkgResult<Self> {
        let mut entries = Vec::new();
        let mut current_name: Option<String> = None;
        let mut current_version: Option<String> = None;
        let mut current_source: Option<String> = None;
        let mut current_checksum: Option<String> = None;

        for line in content.lines() {
            let trimmed = line.trim();

            if trimmed.is_empty() {
                // Flush current entry
                if let (Some(name), Some(version), Some(source)) =
                    (&current_name, &current_version, &current_source)
                {
                    entries.push(LockEntry {
                        name: name.clone(),
                        version: version.clone(),
                        source: source.clone(),
                        checksum: current_checksum.take(),
                    });
                }
                current_name = None;
                current_version = None;
                current_source = None;
                continue;
            }

            if trimmed.starts_with('#') {
                continue;
            }

            if let Some((key, value)) = parse_kv(trimmed) {
                let val = unquote(&value);
                match key.as_str() {
                    "name" => current_name = Some(val),
                    "version" => current_version = Some(val),
                    "source" => current_source = Some(val),
                    "checksum" => current_checksum = Some(val),
                    _ => {}
                }
            }
        }

        // Flush last entry
        if let (Some(name), Some(version), Some(source)) =
            (current_name, current_version, current_source)
        {
            entries.push(LockEntry {
                name,
                version,
                source,
                checksum: current_checksum,
            });
        }

        Ok(Self { entries })
    }

    /// Serialize the lock file to string.
    pub fn serialize(&self) -> String {
        let mut out = String::new();
        out.push_str("# This file is auto-generated by `hexa install`. Do not edit.\n\n");
        for entry in &self.entries {
            out.push_str(&format!("name = \"{}\"\n", entry.name));
            out.push_str(&format!("version = \"{}\"\n", entry.version));
            out.push_str(&format!("source = \"{}\"\n", entry.source));
            if let Some(checksum) = &entry.checksum {
                out.push_str(&format!("checksum = \"{}\"\n", checksum));
            }
            out.push('\n');
        }
        out
    }

    /// Find a lock entry by name.
    pub fn find(&self, name: &str) -> Option<&LockEntry> {
        self.entries.iter().find(|e| e.name == name)
    }
}

// ── Semver types ───────────────────────────────────────────

/// A parsed semantic version: major.minor.patch
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SemVer {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

impl SemVer {
    pub fn new(major: u32, minor: u32, patch: u32) -> Self {
        Self { major, minor, patch }
    }

    /// Parse a version string like "1.2.3", "1.2", or "1".
    pub fn parse(s: &str) -> Option<Self> {
        let parts: Vec<&str> = s.trim().split('.').collect();
        let major = parts.first()?.parse().ok()?;
        let minor = parts.get(1).and_then(|p| p.parse().ok()).unwrap_or(0);
        let patch = parts.get(2).and_then(|p| p.parse().ok()).unwrap_or(0);
        Some(Self { major, minor, patch })
    }

    /// Compare two versions. Returns Ordering.
    pub fn cmp(&self, other: &SemVer) -> std::cmp::Ordering {
        self.major
            .cmp(&other.major)
            .then(self.minor.cmp(&other.minor))
            .then(self.patch.cmp(&other.patch))
    }
}

impl PartialOrd for SemVer {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for SemVer {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        SemVer::cmp(self, other)
    }
}

impl fmt::Display for SemVer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

// ── Version requirement ────────────────────────────────────

/// A version requirement that can match against versions.
#[derive(Debug, Clone)]
pub enum VersionReq {
    /// `*` or `latest` — matches any version
    Any,
    /// `=1.2.3` — exact match
    Exact(SemVer),
    /// `^1.2.3` — compatible (same major, >= minor.patch)
    Caret(SemVer),
    /// `~1.2.3` — patch-level (same major.minor, >= patch)
    Tilde(SemVer),
    /// `>=1.0,<2.0` — range with lower and upper bounds
    Range {
        lower: Option<(SemVer, bool)>, // (version, inclusive)
        upper: Option<(SemVer, bool)>, // (version, inclusive)
    },
}

impl VersionReq {
    /// Parse a version requirement string.
    /// Supports: `*`, `latest`, `=1.2.3`, `^1.2.3`, `~1.2.3`, `>=1.0,<2.0`, bare `1.2.3`
    pub fn parse(s: &str) -> Option<Self> {
        let s = s.trim();

        if s == "*" || s == "latest" {
            return Some(VersionReq::Any);
        }

        // Range: ">=1.0,<2.0" or ">=1.0, <2.0"
        if s.contains(',') {
            return Self::parse_range(s);
        }

        // Exact: =1.2.3
        if let Some(rest) = s.strip_prefix('=') {
            let ver = SemVer::parse(rest.trim())?;
            return Some(VersionReq::Exact(ver));
        }

        // Caret: ^1.2.3
        if let Some(rest) = s.strip_prefix('^') {
            let ver = SemVer::parse(rest.trim())?;
            return Some(VersionReq::Caret(ver));
        }

        // Tilde: ~1.2.3
        if let Some(rest) = s.strip_prefix('~') {
            let ver = SemVer::parse(rest.trim())?;
            return Some(VersionReq::Tilde(ver));
        }

        // Comparison operators without comma (single bound)
        if s.starts_with(">=") || s.starts_with("<=") || s.starts_with('>') || s.starts_with('<') {
            return Self::parse_range(s);
        }

        // Bare version: treat as exact
        let ver = SemVer::parse(s)?;
        Some(VersionReq::Exact(ver))
    }

    /// Parse a range requirement like ">=1.0,<2.0"
    fn parse_range(s: &str) -> Option<Self> {
        let mut lower: Option<(SemVer, bool)> = None;
        let mut upper: Option<(SemVer, bool)> = None;

        let parts: Vec<&str> = s.split(',').collect();
        for part in parts {
            let part = part.trim();
            if let Some(rest) = part.strip_prefix(">=") {
                lower = Some((SemVer::parse(rest.trim())?, true));
            } else if let Some(rest) = part.strip_prefix('>') {
                lower = Some((SemVer::parse(rest.trim())?, false));
            } else if let Some(rest) = part.strip_prefix("<=") {
                upper = Some((SemVer::parse(rest.trim())?, true));
            } else if let Some(rest) = part.strip_prefix('<') {
                upper = Some((SemVer::parse(rest.trim())?, false));
            }
        }

        if lower.is_some() || upper.is_some() {
            Some(VersionReq::Range { lower, upper })
        } else {
            None
        }
    }

    /// Check if a version matches this requirement.
    pub fn matches(&self, version: &SemVer) -> bool {
        match self {
            VersionReq::Any => true,
            VersionReq::Exact(req) => version == req,
            VersionReq::Caret(req) => {
                if version.major != req.major {
                    return false;
                }
                if version.minor < req.minor {
                    return false;
                }
                if version.minor == req.minor && version.patch < req.patch {
                    return false;
                }
                true
            }
            VersionReq::Tilde(req) => {
                if version.major != req.major || version.minor != req.minor {
                    return false;
                }
                version.patch >= req.patch
            }
            VersionReq::Range { lower, upper } => {
                if let Some((lo, inclusive)) = lower {
                    if *inclusive {
                        if version.cmp(lo) == std::cmp::Ordering::Less {
                            return false;
                        }
                    } else if version.cmp(lo) != std::cmp::Ordering::Greater {
                        return false;
                    }
                }
                if let Some((hi, inclusive)) = upper {
                    if *inclusive {
                        if version.cmp(hi) == std::cmp::Ordering::Greater {
                            return false;
                        }
                    } else if version.cmp(hi) != std::cmp::Ordering::Less {
                        return false;
                    }
                }
                true
            }
        }
    }
}

impl fmt::Display for VersionReq {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            VersionReq::Any => write!(f, "*"),
            VersionReq::Exact(v) => write!(f, "={}", v),
            VersionReq::Caret(v) => write!(f, "^{}", v),
            VersionReq::Tilde(v) => write!(f, "~{}", v),
            VersionReq::Range { lower, upper } => {
                let mut parts = Vec::new();
                if let Some((v, true)) = lower {
                    parts.push(format!(">={}", v));
                } else if let Some((v, false)) = lower {
                    parts.push(format!(">{}", v));
                }
                if let Some((v, true)) = upper {
                    parts.push(format!("<={}", v));
                } else if let Some((v, false)) = upper {
                    parts.push(format!("<{}", v));
                }
                write!(f, "{}", parts.join(","))
            }
        }
    }
}

// ── Dependency conflict resolution ─────────────────────────

/// Given multiple version requirements for the same package and a list of
/// available versions, resolve to the highest compatible version.
pub fn resolve_version_conflict(
    requirements: &[VersionReq],
    available: &[SemVer],
) -> Option<SemVer> {
    let mut candidates: Vec<SemVer> = available
        .iter()
        .filter(|v| requirements.iter().all(|req| req.matches(v)))
        .copied()
        .collect();

    candidates.sort();
    candidates.last().copied()
}

// ── Version matching (legacy compat) ───────────────────────

/// Check if a version matches a version requirement.
/// Supports exact match ("1.0.0"), caret ("^1.0"), tilde ("~1.0"),
/// range (">=1.0,<2.0"), and wildcards ("*", "latest").
pub fn version_matches(version: &str, requirement: &str) -> bool {
    let req = requirement.trim();

    // "latest" matches anything
    if req == "latest" || req == "*" {
        return true;
    }

    // Try the new semver parser
    if let (Some(ver), Some(vreq)) = (SemVer::parse(version), VersionReq::parse(req)) {
        return vreq.matches(&ver);
    }

    // Fallback: prefix match
    version.starts_with(req) || version == req
}

/// Parse "major.minor.patch" into (u32, u32, u32). Missing parts default to 0.
fn parse_semver(s: &str) -> Option<(u32, u32, u32)> {
    let v = SemVer::parse(s)?;
    Some((v.major, v.minor, v.patch))
}

// ── Dependency resolver ─────────────────────────────────────

/// The package directory where fetched dependencies are stored.
pub const PACKAGES_DIR: &str = ".hexa-packages";

/// Resolve all dependencies from a manifest.
/// Returns the list of resolved paths that should be added to the import search path.
pub fn resolve_dependencies(
    manifest: &Manifest,
    project_dir: &Path,
) -> PkgResult<Vec<ResolvedDep>> {
    let mut resolved = Vec::new();

    for dep in &manifest.dependencies {
        let r = resolve_single(dep, project_dir)?;
        resolved.push(r);
    }

    Ok(resolved)
}

/// A resolved dependency with its local path.
#[derive(Debug, Clone)]
pub struct ResolvedDep {
    pub name: String,
    pub version: String,
    pub source_desc: String, // For lock file
    pub local_path: PathBuf, // Where the package files live
}

/// Resolve a single dependency.
fn resolve_single(dep: &Dependency, project_dir: &Path) -> PkgResult<ResolvedDep> {
    match &dep.source {
        DepSource::Path { path } => {
            let resolved_path = if Path::new(path).is_absolute() {
                PathBuf::from(path)
            } else {
                project_dir.join(path)
            };

            if !resolved_path.exists() {
                return Err(PackageError::new(format!(
                    "path dependency '{}': directory not found at {}",
                    dep.name,
                    resolved_path.display()
                )));
            }

            // Read version from the dependency's own hexa.toml if it exists
            let dep_version = read_dep_version(&resolved_path).unwrap_or_else(|| "0.0.0".to_string());

            Ok(ResolvedDep {
                name: dep.name.clone(),
                version: dep_version,
                source_desc: format!("path+{}", resolved_path.display()),
                local_path: resolved_path,
            })
        }
        DepSource::Git { version, url } => {
            let pkg_dir = project_dir.join(PACKAGES_DIR).join(&dep.name);

            if pkg_dir.exists() {
                // Already cloned, check version
                let existing_ver = read_dep_version(&pkg_dir).unwrap_or_else(|| "0.0.0".to_string());
                if version_matches(&existing_ver, version) || version == "latest" {
                    let commit = read_git_commit(&pkg_dir).unwrap_or_else(|| "unknown".to_string());
                    return Ok(ResolvedDep {
                        name: dep.name.clone(),
                        version: existing_ver,
                        source_desc: format!("git+{}#{}", url, commit),
                        local_path: pkg_dir,
                    });
                }
            }

            // Clone the repository
            fs::create_dir_all(project_dir.join(PACKAGES_DIR)).map_err(|e| {
                PackageError::new(format!("cannot create {}: {}", PACKAGES_DIR, e))
            })?;

            // Remove old clone if exists
            if pkg_dir.exists() {
                let _ = fs::remove_dir_all(&pkg_dir);
            }

            let status = std::process::Command::new("git")
                .args(["clone", "--depth", "1", url, &pkg_dir.to_string_lossy()])
                .status()
                .map_err(|e| PackageError::new(format!("git clone failed: {}", e)))?;

            if !status.success() {
                return Err(PackageError::new(format!(
                    "git clone failed for '{}' ({})",
                    dep.name, url
                )));
            }

            let cloned_ver = read_dep_version(&pkg_dir).unwrap_or_else(|| "0.0.0".to_string());
            let commit = read_git_commit(&pkg_dir).unwrap_or_else(|| "unknown".to_string());

            Ok(ResolvedDep {
                name: dep.name.clone(),
                version: cloned_ver,
                source_desc: format!("git+{}#{}", url, commit),
                local_path: pkg_dir,
            })
        }
        DepSource::Registry { version } => {
            // For v1, registry deps look in the local packages/ directory first,
            // then in .hexa-packages/. Future: download from GitHub releases.
            let local_pkg = project_dir.join("packages").join(&dep.name);
            if local_pkg.exists() {
                let pkg_ver = read_dep_version(&local_pkg).unwrap_or_else(|| "0.0.0".to_string());
                return Ok(ResolvedDep {
                    name: dep.name.clone(),
                    version: pkg_ver,
                    source_desc: "registry".to_string(),
                    local_path: local_pkg,
                });
            }

            let cached_pkg = project_dir.join(PACKAGES_DIR).join(&dep.name);
            if cached_pkg.exists() {
                let pkg_ver = read_dep_version(&cached_pkg).unwrap_or_else(|| "0.0.0".to_string());
                if version_matches(&pkg_ver, version) {
                    return Ok(ResolvedDep {
                        name: dep.name.clone(),
                        version: pkg_ver,
                        source_desc: "registry".to_string(),
                        local_path: cached_pkg,
                    });
                }
            }

            Err(PackageError::new(format!(
                "dependency '{}' ({}) not found. Install with: hexa install\n  \
                 Looked in: packages/{0}, {1}/{0}",
                dep.name, PACKAGES_DIR
            )))
        }
    }
}

/// Read version from a package directory's hexa.toml.
fn read_dep_version(dir: &Path) -> Option<String> {
    let toml_path = dir.join("hexa.toml");
    let content = fs::read_to_string(toml_path).ok()?;
    let manifest = parse_manifest(&content).ok()?;
    Some(manifest.package.version)
}

/// Read the current git commit hash from a directory.
fn read_git_commit(dir: &Path) -> Option<String> {
    let output = std::process::Command::new("git")
        .args(["rev-parse", "HEAD"])
        .current_dir(dir)
        .output()
        .ok()?;
    if output.status.success() {
        Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
    } else {
        None
    }
}

// ── Module search paths ─────────────────────────────────────

/// Build the list of directories to search when resolving `use` statements.
/// Order: source_dir > packages/ > .hexa-packages/ > resolved deps
pub fn build_search_paths(
    source_dir: Option<&str>,
    project_dir: &Path,
    resolved_deps: &[ResolvedDep],
) -> Vec<PathBuf> {
    let mut paths = Vec::new();

    // 1. Source directory (for local modules)
    if let Some(sd) = source_dir {
        paths.push(PathBuf::from(sd));
    }

    // 2. Local packages/ directory
    let packages = project_dir.join("packages");
    if packages.exists() {
        paths.push(packages);
    }

    // 3. .hexa-packages/ directory
    let hexa_packages = project_dir.join(PACKAGES_DIR);
    if hexa_packages.exists() {
        paths.push(hexa_packages);
    }

    // 4. Each resolved dependency's path
    for dep in resolved_deps {
        // Add the dep's src/ subdirectory if it exists, otherwise the dep root
        let src_dir = dep.local_path.join("src");
        if src_dir.exists() {
            paths.push(src_dir);
        }
        paths.push(dep.local_path.clone());
    }

    paths
}

/// Resolve a module path to a file, searching through the provided paths.
/// Returns the first matching `.hexa` file found.
pub fn resolve_module_file(module_path: &[String], search_paths: &[PathBuf]) -> Option<PathBuf> {
    let rel_path = module_path.join("/");

    for base in search_paths {
        // Try: base/module_path.hexa
        let candidate = base.join(format!("{}.hexa", rel_path));
        if candidate.exists() {
            return Some(candidate);
        }

        // Try: base/module_path/mod.hexa (directory module)
        let dir_candidate = base.join(&rel_path).join("mod.hexa");
        if dir_candidate.exists() {
            return Some(dir_candidate);
        }

        // For single-segment paths, also try base/name/name.hexa (package root)
        if module_path.len() == 1 {
            let pkg_candidate = base.join(&module_path[0]).join(format!("{}.hexa", &module_path[0]));
            if pkg_candidate.exists() {
                return Some(pkg_candidate);
            }
            // And base/name/src/lib.hexa
            let lib_candidate = base.join(&module_path[0]).join("src").join("lib.hexa");
            if lib_candidate.exists() {
                return Some(lib_candidate);
            }
            // And base/name/lib.hexa
            let lib2_candidate = base.join(&module_path[0]).join("lib.hexa");
            if lib2_candidate.exists() {
                return Some(lib2_candidate);
            }
        }
    }

    None
}

// ── CLI: hexa install ───────────────────────────────────────

/// Fetch all dependencies listed in hexa.toml.
pub fn cmd_install(project_dir: &Path) -> PkgResult<()> {
    let toml_path = project_dir.join("hexa.toml");
    if !toml_path.exists() {
        return Err(PackageError::new(
            "no hexa.toml found in current directory\nRun 'hexa init <name>' to create a new project",
        ));
    }

    let content = fs::read_to_string(&toml_path)
        .map_err(|e| PackageError::new(format!("cannot read hexa.toml: {}", e)))?;
    let manifest = parse_manifest(&content)?;

    if manifest.dependencies.is_empty() {
        println!("No dependencies to install.");
        return Ok(());
    }

    println!("Resolving {} dependencies...", manifest.dependencies.len());

    let resolved = resolve_dependencies(&manifest, project_dir)?;

    // Generate lock file
    let mut lock = LockFile::new();
    for dep in &resolved {
        lock.entries.push(LockEntry {
            name: dep.name.clone(),
            version: dep.version.clone(),
            source: dep.source_desc.clone(),
            checksum: None,
        });
    }

    let lock_path = project_dir.join("hexa.lock");
    fs::write(&lock_path, lock.serialize())
        .map_err(|e| PackageError::new(format!("cannot write hexa.lock: {}", e)))?;

    println!("\nInstalled {} dependencies:", resolved.len());
    for dep in &resolved {
        println!("  {} v{} ({})", dep.name, dep.version, dep.source_desc);
    }
    println!("\nGenerated hexa.lock");

    Ok(())
}

// ── CLI: hexa add ───────────────────────────────────────────

/// Add a dependency to hexa.toml.
/// Supports formats:
///   `hexa add pkg` -> registry "latest"
///   `hexa add pkg@1.0` -> registry "^1.0"
///   `hexa add pkg --path ../dir` -> path dep
///   `hexa add pkg --git https://...` -> git dep
pub fn cmd_add_pkg(
    pkg_spec: &str,
    path_opt: Option<&str>,
    git_opt: Option<&str>,
    project_dir: &Path,
) -> PkgResult<()> {
    let toml_path = project_dir.join("hexa.toml");
    if !toml_path.exists() {
        return Err(PackageError::new(
            "no hexa.toml found in current directory\nRun 'hexa init <name>' to create a new project",
        ));
    }

    let content = fs::read_to_string(&toml_path)
        .map_err(|e| PackageError::new(format!("cannot read hexa.toml: {}", e)))?;
    let mut manifest = parse_manifest(&content)?;

    // Parse package name and version from spec (e.g., "pkg@1.0")
    let (name, version) = if let Some(at_pos) = pkg_spec.find('@') {
        (
            pkg_spec[..at_pos].to_string(),
            pkg_spec[at_pos + 1..].to_string(),
        )
    } else {
        (pkg_spec.to_string(), "latest".to_string())
    };

    // Check if already present
    if manifest.dependencies.iter().any(|d| d.name == name) {
        println!("Dependency '{}' already exists in hexa.toml", name);
        return Ok(());
    }

    // Determine source
    let source = if let Some(path) = path_opt {
        DepSource::Path {
            path: path.to_string(),
        }
    } else if let Some(git) = git_opt {
        DepSource::Git {
            version,
            url: git.to_string(),
        }
    } else {
        DepSource::Registry { version }
    };

    manifest.dependencies.push(Dependency {
        name: name.clone(),
        source,
    });

    // Write back
    let new_content = serialize_manifest(&manifest);
    fs::write(&toml_path, &new_content)
        .map_err(|e| PackageError::new(format!("cannot write hexa.toml: {}", e)))?;

    // Write hexa.lock with resolved versions
    let lock_path = project_dir.join("hexa.lock");
    let mut lock = if lock_path.exists() {
        let lock_content = fs::read_to_string(&lock_path).unwrap_or_default();
        LockFile::parse(&lock_content).unwrap_or_else(|_| LockFile::new())
    } else {
        LockFile::new()
    };

    // Add lock entry for the new dependency
    if !lock.entries.iter().any(|e| e.name == name) {
        let resolved_version = match &manifest.dependencies.last().unwrap().source {
            DepSource::Registry { version } => {
                if version == "latest" { "0.0.0".to_string() } else { version.clone() }
            }
            DepSource::Git { version, .. } => version.clone(),
            DepSource::Path { .. } => "0.0.0".to_string(),
        };
        lock.entries.push(LockEntry {
            name: name.clone(),
            version: resolved_version,
            source: "registry".to_string(),
            checksum: None,
        });
    }

    fs::write(&lock_path, lock.serialize())
        .map_err(|e| PackageError::new(format!("cannot write hexa.lock: {}", e)))?;

    println!("Added '{}' to [dependencies]", name);
    println!("Updated hexa.lock");
    Ok(())
}

// ── CLI: hexa publish ───────────────────────────────────────

/// Package the project into a distributable tarball.
pub fn cmd_publish_pkg(project_dir: &Path) -> PkgResult<()> {
    let toml_path = project_dir.join("hexa.toml");
    if !toml_path.exists() {
        return Err(PackageError::new("no hexa.toml found in current directory"));
    }

    let content = fs::read_to_string(&toml_path)
        .map_err(|e| PackageError::new(format!("cannot read hexa.toml: {}", e)))?;
    let manifest = parse_manifest(&content)?;

    let name = &manifest.package.name;
    let version = &manifest.package.version;

    // Collect all .hexa files
    let mut files = Vec::new();
    files.push("hexa.toml".to_string());

    // Collect src/ files
    collect_hexa_files(&project_dir.join("src"), "src", &mut files);

    // Create package directory
    let pkg_filename = format!("{}-{}.tar", name, version);
    let pkg_dir = project_dir.join("target").join("package");
    fs::create_dir_all(&pkg_dir)
        .map_err(|e| PackageError::new(format!("cannot create target/package: {}", e)))?;

    // Write a simple package manifest (JSON)
    let pkg_manifest = format!(
        "{{\n  \"name\": \"{}\",\n  \"version\": \"{}\",\n  \"files\": [{}]\n}}\n",
        name,
        version,
        files
            .iter()
            .map(|f| format!("\"{}\"", f))
            .collect::<Vec<_>>()
            .join(", ")
    );

    let manifest_path = pkg_dir.join("package.json");
    fs::write(&manifest_path, &pkg_manifest)
        .map_err(|e| PackageError::new(format!("cannot write package.json: {}", e)))?;

    // Create a simple tar-like archive (concatenated files with headers)
    let archive_path = pkg_dir.join(&pkg_filename);
    let mut archive = fs::File::create(&archive_path)
        .map_err(|e| PackageError::new(format!("cannot create {}: {}", pkg_filename, e)))?;

    // Write a simple text-based archive format (for portability)
    writeln!(archive, "# HEXA Package: {} v{}", name, version)
        .map_err(|e| PackageError::new(format!("write error: {}", e)))?;
    writeln!(archive, "# Files: {}", files.len())
        .map_err(|e| PackageError::new(format!("write error: {}", e)))?;
    writeln!(archive)
        .map_err(|e| PackageError::new(format!("write error: {}", e)))?;

    for file in &files {
        let file_path = project_dir.join(file);
        if let Ok(content) = fs::read_to_string(&file_path) {
            writeln!(archive, "--- {} ---", file)
                .map_err(|e| PackageError::new(format!("write error: {}", e)))?;
            writeln!(archive, "{}", content)
                .map_err(|e| PackageError::new(format!("write error: {}", e)))?;
        }
    }

    println!("Package created: target/package/{}", pkg_filename);
    println!("  Name: {}", name);
    println!("  Version: {}", version);
    println!("  Files: {}", files.len());
    println!();
    println!("To publish:");
    println!("  1. Create a GitHub repository for '{}'", name);
    println!("  2. Push your code: git push origin main");
    println!("  3. Create a release: git tag v{} && git push --tags", version);
    println!("  4. Others can install: hexa add {} --git <repo-url>", name);

    Ok(())
}

/// Recursively collect .hexa files from a directory.
fn collect_hexa_files(dir: &Path, prefix: &str, files: &mut Vec<String>) {
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            let rel = format!("{}/{}", prefix, entry.file_name().to_string_lossy());
            if path.is_dir() {
                collect_hexa_files(&path, &rel, files);
            } else if path.extension().map_or(false, |e| e == "hexa") {
                files.push(rel);
            }
        }
    }
}

// ── CLI: hexa search ────────────────────────────────────────

/// Search GitHub for hexa packages.
pub fn cmd_search(query: &str) -> PkgResult<()> {
    println!("Searching GitHub for hexa packages matching '{}'...\n", query);

    // Use GitHub search API via ureq
    let search_url = format!(
        "https://api.github.com/search/repositories?q={}+language:hexa+topic:hexa-package&sort=stars",
        query
    );

    let response = ureq::get(&search_url)
        .set("Accept", "application/vnd.github.v3+json")
        .set("User-Agent", "hexa-lang-package-manager")
        .call();

    match response {
        Ok(resp) => {
            let body = resp.into_string().unwrap_or_default();
            // Simple JSON parsing for search results
            print_search_results(&body, query);
        }
        Err(_) => {
            // Fallback: show manual search instructions
            println!("Could not reach GitHub API.");
            println!("Search manually: https://github.com/search?q={}&type=repositories", query);
            println!("\nTo add a git dependency:");
            println!("  hexa add {} --git https://github.com/user/repo", query);
        }
    }

    Ok(())
}

/// Extract and print search results from GitHub API JSON response.
fn print_search_results(json: &str, query: &str) {
    // Simple extraction without a full JSON parser for the search display
    let count = extract_json_number(json, "total_count").unwrap_or(0);

    if count == 0 {
        println!("No packages found for '{}'.", query);
        println!("\nTo create a hexa package:");
        println!("  1. hexa init my-package");
        println!("  2. Add topic 'hexa-package' to your GitHub repo");
        println!("  3. hexa publish");
        return;
    }

    println!("Found {} results:\n", count);

    // Extract items (simplified parsing)
    let mut pos = 0;
    let mut shown = 0;
    while shown < 10 {
        if let Some(start) = json[pos..].find("\"full_name\"") {
            let abs_start = pos + start;
            if let Some(name) = extract_json_string(json, abs_start) {
                let desc_search_start = abs_start + name.len();
                let description = json[desc_search_start..]
                    .find("\"description\"")
                    .and_then(|ds| extract_json_string(json, desc_search_start + ds))
                    .unwrap_or_default();

                let stars = json[desc_search_start..]
                    .find("\"stargazers_count\"")
                    .and_then(|ss| {
                        let s = desc_search_start + ss;
                        extract_json_number(json, "stargazers_count")
                            .or_else(|| {
                                json[s..].find(':').and_then(|c| {
                                    let after = json[s + c + 1..].trim_start();
                                    after.split(|c: char| !c.is_ascii_digit()).next()?.parse().ok()
                                })
                            })
                    })
                    .unwrap_or(0);

                println!("  {} ({}*)", name, stars);
                if !description.is_empty() {
                    println!("    {}", description);
                }
                println!();
                shown += 1;
                pos = abs_start + 50; // Skip past this entry
            } else {
                break;
            }
        } else {
            break;
        }
    }

    println!("Install with: hexa add <name> --git https://github.com/<full_name>");
}

/// Extract a JSON string value starting from a position with "key": "value" format.
fn extract_json_string(json: &str, start: usize) -> Option<String> {
    let after_key = json[start..].find(':')?;
    let value_start = start + after_key + 1;
    let trimmed = json[value_start..].trim_start();
    if trimmed.starts_with('"') {
        let content_start = 1;
        let content_end = trimmed[content_start..].find('"')?;
        Some(trimmed[content_start..content_start + content_end].to_string())
    } else {
        None
    }
}

/// Extract a JSON number value by key name (first occurrence).
fn extract_json_number(json: &str, key: &str) -> Option<u64> {
    let pattern = format!("\"{}\"", key);
    let pos = json.find(&pattern)?;
    let after = &json[pos + pattern.len()..];
    let colon = after.find(':')?;
    let value_str = after[colon + 1..].trim_start();
    value_str
        .split(|c: char| !c.is_ascii_digit())
        .next()?
        .parse()
        .ok()
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_manifest_basic() {
        let content = r#"
[package]
name = "my-project"
version = "0.1.0"
description = "Test project"

[dependencies]
foo = "1.0"
bar = "^2.0"
"#;
        let manifest = parse_manifest(content).unwrap();
        assert_eq!(manifest.package.name, "my-project");
        assert_eq!(manifest.package.version, "0.1.0");
        assert_eq!(manifest.package.description.as_deref(), Some("Test project"));
        assert_eq!(manifest.dependencies.len(), 2);
        assert_eq!(manifest.dependencies[0].name, "foo");
        assert_eq!(manifest.dependencies[1].name, "bar");
        match &manifest.dependencies[0].source {
            DepSource::Registry { version } => assert_eq!(version, "1.0"),
            _ => panic!("expected Registry"),
        }
    }

    #[test]
    fn test_parse_manifest_git_dep() {
        let content = r#"
[package]
name = "test"
version = "1.0.0"

[dependencies]
math-extras = { version = "2.0", git = "https://github.com/user/repo" }
"#;
        let manifest = parse_manifest(content).unwrap();
        assert_eq!(manifest.dependencies.len(), 1);
        match &manifest.dependencies[0].source {
            DepSource::Git { version, url } => {
                assert_eq!(version, "2.0");
                assert_eq!(url, "https://github.com/user/repo");
            }
            _ => panic!("expected Git"),
        }
    }

    #[test]
    fn test_parse_manifest_path_dep() {
        let content = r#"
[package]
name = "test"
version = "1.0.0"

[dependencies]
local-lib = { path = "../local-lib" }
"#;
        let manifest = parse_manifest(content).unwrap();
        assert_eq!(manifest.dependencies.len(), 1);
        match &manifest.dependencies[0].source {
            DepSource::Path { path } => assert_eq!(path, "../local-lib"),
            _ => panic!("expected Path"),
        }
    }

    #[test]
    fn test_parse_manifest_mixed_deps() {
        let content = r#"
[package]
name = "test"

[dependencies]
registry-pkg = "1.0"
git-pkg = { version = "2.0", git = "https://example.com/repo" }
local-pkg = { path = "./libs/local" }
"#;
        let manifest = parse_manifest(content).unwrap();
        assert_eq!(manifest.dependencies.len(), 3);
        assert!(matches!(&manifest.dependencies[0].source, DepSource::Registry { .. }));
        assert!(matches!(&manifest.dependencies[1].source, DepSource::Git { .. }));
        assert!(matches!(&manifest.dependencies[2].source, DepSource::Path { .. }));
    }

    #[test]
    fn test_parse_manifest_no_deps() {
        let content = "[package]\nname = \"test\"\nversion = \"0.1.0\"\n";
        let manifest = parse_manifest(content).unwrap();
        assert_eq!(manifest.package.name, "test");
        assert!(manifest.dependencies.is_empty());
    }

    #[test]
    fn test_parse_manifest_missing_name() {
        let content = "[package]\nversion = \"0.1.0\"\n";
        assert!(parse_manifest(content).is_err());
    }

    #[test]
    fn test_serialize_manifest() {
        let manifest = Manifest {
            package: PackageInfo {
                name: "test-pkg".to_string(),
                version: "1.2.3".to_string(),
                description: Some("A test".to_string()),
            },
            dependencies: vec![
                Dependency {
                    name: "foo".to_string(),
                    source: DepSource::Registry {
                        version: "1.0".to_string(),
                    },
                },
                Dependency {
                    name: "bar".to_string(),
                    source: DepSource::Path {
                        path: "../bar".to_string(),
                    },
                },
            ],
        };
        let s = serialize_manifest(&manifest);
        assert!(s.contains("name = \"test-pkg\""));
        assert!(s.contains("version = \"1.2.3\""));
        assert!(s.contains("description = \"A test\""));
        assert!(s.contains("foo = \"1.0\""));
        assert!(s.contains("bar = { path = \"../bar\" }"));
    }

    #[test]
    fn test_roundtrip_manifest() {
        let content = r#"[package]
name = "roundtrip"
version = "0.1.0"
description = "Test roundtrip"

[dependencies]
dep-a = "1.0"
dep-b = { path = "../lib" }
dep-c = { version = "3.0", git = "https://example.com/repo" }
"#;
        let manifest = parse_manifest(content).unwrap();
        let serialized = serialize_manifest(&manifest);
        let reparsed = parse_manifest(&serialized).unwrap();
        assert_eq!(reparsed.package.name, "roundtrip");
        assert_eq!(reparsed.dependencies.len(), 3);
    }

    #[test]
    fn test_version_matches_exact() {
        assert!(version_matches("1.0.0", "1.0.0"));
        assert!(version_matches("1.0.0", "=1.0.0"));
        assert!(!version_matches("1.0.0", "2.0.0"));
        assert!(!version_matches("1.1.0", "1.0.0"));
    }

    #[test]
    fn test_version_matches_caret() {
        assert!(version_matches("1.0.0", "^1.0"));
        assert!(version_matches("1.5.0", "^1.0"));
        assert!(version_matches("1.0.1", "^1.0"));
        assert!(!version_matches("2.0.0", "^1.0"));
        assert!(!version_matches("0.9.0", "^1.0"));
    }

    #[test]
    fn test_version_matches_tilde() {
        assert!(version_matches("1.2.3", "~1.2.3"));
        assert!(version_matches("1.2.5", "~1.2.3"));
        assert!(!version_matches("1.3.0", "~1.2.3"));
        assert!(!version_matches("2.0.0", "~1.2.3"));
        assert!(!version_matches("1.2.2", "~1.2.3"));
    }

    #[test]
    fn test_version_matches_range() {
        assert!(version_matches("1.5.0", ">=1.0.0,<2.0.0"));
        assert!(version_matches("1.0.0", ">=1.0.0,<2.0.0"));
        assert!(!version_matches("2.0.0", ">=1.0.0,<2.0.0"));
        assert!(!version_matches("0.9.0", ">=1.0.0,<2.0.0"));
        assert!(version_matches("1.9.9", ">=1.0,<2.0"));
    }

    #[test]
    fn test_version_matches_latest() {
        assert!(version_matches("1.0.0", "latest"));
        assert!(version_matches("99.99.99", "latest"));
        assert!(version_matches("0.0.1", "*"));
    }

    // ── SemVer tests ──

    #[test]
    fn test_semver_parse() {
        let v = SemVer::parse("1.2.3").unwrap();
        assert_eq!(v.major, 1);
        assert_eq!(v.minor, 2);
        assert_eq!(v.patch, 3);

        let v2 = SemVer::parse("1.0").unwrap();
        assert_eq!(v2, SemVer::new(1, 0, 0));

        let v3 = SemVer::parse("5").unwrap();
        assert_eq!(v3, SemVer::new(5, 0, 0));

        assert!(SemVer::parse("abc").is_none());
    }

    #[test]
    fn test_semver_ordering() {
        let v1 = SemVer::new(1, 0, 0);
        let v2 = SemVer::new(1, 1, 0);
        let v3 = SemVer::new(2, 0, 0);
        assert!(v1 < v2);
        assert!(v2 < v3);
        assert!(v1 < v3);
        assert_eq!(v1, SemVer::new(1, 0, 0));
    }

    #[test]
    fn test_semver_display() {
        let v = SemVer::new(1, 2, 3);
        assert_eq!(format!("{}", v), "1.2.3");
    }

    // ── VersionReq tests ──

    #[test]
    fn test_version_req_parse_caret() {
        let req = VersionReq::parse("^1.2.3").unwrap();
        assert!(matches!(req, VersionReq::Caret(_)));
        let v = SemVer::new(1, 2, 3);
        assert!(req.matches(&v));
        assert!(req.matches(&SemVer::new(1, 5, 0)));
        assert!(!req.matches(&SemVer::new(2, 0, 0)));
    }

    #[test]
    fn test_version_req_parse_tilde() {
        let req = VersionReq::parse("~1.2.3").unwrap();
        assert!(matches!(req, VersionReq::Tilde(_)));
        assert!(req.matches(&SemVer::new(1, 2, 3)));
        assert!(req.matches(&SemVer::new(1, 2, 9)));
        assert!(!req.matches(&SemVer::new(1, 3, 0)));
    }

    #[test]
    fn test_version_req_parse_exact() {
        let req = VersionReq::parse("=1.2.3").unwrap();
        assert!(req.matches(&SemVer::new(1, 2, 3)));
        assert!(!req.matches(&SemVer::new(1, 2, 4)));
    }

    #[test]
    fn test_version_req_parse_range() {
        let req = VersionReq::parse(">=1.0.0,<2.0.0").unwrap();
        assert!(req.matches(&SemVer::new(1, 0, 0)));
        assert!(req.matches(&SemVer::new(1, 9, 9)));
        assert!(!req.matches(&SemVer::new(2, 0, 0)));
        assert!(!req.matches(&SemVer::new(0, 9, 0)));
    }

    #[test]
    fn test_version_req_any() {
        let req = VersionReq::parse("*").unwrap();
        assert!(req.matches(&SemVer::new(0, 0, 1)));
        assert!(req.matches(&SemVer::new(99, 99, 99)));

        let req2 = VersionReq::parse("latest").unwrap();
        assert!(req2.matches(&SemVer::new(1, 0, 0)));
    }

    #[test]
    fn test_version_req_display() {
        assert_eq!(format!("{}", VersionReq::parse("^1.2.3").unwrap()), "^1.2.3");
        assert_eq!(format!("{}", VersionReq::parse("~1.2.3").unwrap()), "~1.2.3");
        assert_eq!(format!("{}", VersionReq::parse("=1.2.3").unwrap()), "=1.2.3");
        assert_eq!(format!("{}", VersionReq::parse("*").unwrap()), "*");
    }

    // ── Conflict resolution tests ──

    #[test]
    fn test_resolve_version_conflict() {
        let available = vec![
            SemVer::new(1, 0, 0),
            SemVer::new(1, 2, 0),
            SemVer::new(1, 5, 0),
            SemVer::new(2, 0, 0),
        ];
        let reqs = vec![
            VersionReq::parse("^1.0").unwrap(),
            VersionReq::parse(">=1.2.0,<2.0.0").unwrap(),
        ];
        let resolved = resolve_version_conflict(&reqs, &available);
        assert_eq!(resolved, Some(SemVer::new(1, 5, 0)));
    }

    #[test]
    fn test_resolve_version_conflict_no_match() {
        let available = vec![SemVer::new(1, 0, 0), SemVer::new(2, 0, 0)];
        let reqs = vec![
            VersionReq::parse("^1.0").unwrap(),
            VersionReq::parse("^2.0").unwrap(),
        ];
        let resolved = resolve_version_conflict(&reqs, &available);
        assert_eq!(resolved, None); // No version satisfies both
    }

    #[test]
    fn test_resolve_picks_highest() {
        let available = vec![
            SemVer::new(1, 0, 0),
            SemVer::new(1, 1, 0),
            SemVer::new(1, 2, 0),
        ];
        let reqs = vec![VersionReq::parse("^1.0").unwrap()];
        let resolved = resolve_version_conflict(&reqs, &available);
        assert_eq!(resolved, Some(SemVer::new(1, 2, 0)));
    }

    #[test]
    fn test_lock_file_roundtrip() {
        let lock = LockFile {
            entries: vec![
                LockEntry {
                    name: "foo".to_string(),
                    version: "1.0.0".to_string(),
                    source: "registry".to_string(),
                    checksum: Some("abc123".to_string()),
                },
                LockEntry {
                    name: "bar".to_string(),
                    version: "2.0.0".to_string(),
                    source: "git+https://example.com#def456".to_string(),
                    checksum: None,
                },
            ],
        };

        let serialized = lock.serialize();
        let parsed = LockFile::parse(&serialized).unwrap();
        assert_eq!(parsed.entries.len(), 2);
        assert_eq!(parsed.entries[0].name, "foo");
        assert_eq!(parsed.entries[0].version, "1.0.0");
        assert_eq!(parsed.entries[0].checksum.as_deref(), Some("abc123"));
        assert_eq!(parsed.entries[1].name, "bar");
        assert_eq!(parsed.entries[1].source, "git+https://example.com#def456");
    }

    #[test]
    fn test_lock_file_find() {
        let lock = LockFile {
            entries: vec![LockEntry {
                name: "test".to_string(),
                version: "1.0.0".to_string(),
                source: "registry".to_string(),
                checksum: None,
            }],
        };
        assert!(lock.find("test").is_some());
        assert!(lock.find("missing").is_none());
    }

    #[test]
    fn test_parse_semver() {
        assert_eq!(parse_semver("1.2.3"), Some((1, 2, 3)));
        assert_eq!(parse_semver("1.0"), Some((1, 0, 0)));
        assert_eq!(parse_semver("2"), Some((2, 0, 0)));
        assert_eq!(parse_semver("abc"), None);
    }

    #[test]
    fn test_unquote() {
        assert_eq!(unquote("\"hello\""), "hello");
        assert_eq!(unquote("hello"), "hello");
        assert_eq!(unquote("  \"test\"  "), "test");
    }

    #[test]
    fn test_parse_inline_table() {
        let fields = parse_inline_table("version = \"1.0\", git = \"https://example.com\"");
        assert_eq!(fields.get("version").map(|v| unquote(v)), Some("1.0".to_string()));
        assert_eq!(
            fields.get("git").map(|v| unquote(v)),
            Some("https://example.com".to_string())
        );
    }

    #[test]
    fn test_resolve_module_file() {
        // Create temp directory structure
        let tmp = std::env::temp_dir().join("hexa_pkg_test");
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(tmp.join("src")).unwrap();
        fs::create_dir_all(tmp.join("packages/mylib/src")).unwrap();
        fs::write(tmp.join("src/utils.hexa"), "pub fn helper() { 42 }").unwrap();
        fs::write(tmp.join("packages/mylib/src/lib.hexa"), "pub fn greet() { \"hi\" }").unwrap();

        let search_paths = vec![tmp.join("src"), tmp.join("packages")];

        // Should find src/utils.hexa
        let result = resolve_module_file(&["utils".to_string()], &search_paths);
        assert!(result.is_some());

        // Should find packages/mylib/src/lib.hexa
        let result = resolve_module_file(&["mylib".to_string()], &search_paths);
        assert!(result.is_some());

        // Cleanup
        let _ = fs::remove_dir_all(&tmp);
    }

    #[test]
    fn test_build_search_paths() {
        let project_dir = Path::new("/tmp/test-project");
        let deps = vec![ResolvedDep {
            name: "mylib".to_string(),
            version: "1.0.0".to_string(),
            source_desc: "path+/tmp/mylib".to_string(),
            local_path: PathBuf::from("/tmp/mylib"),
        }];

        let paths = build_search_paths(Some("/tmp/test-project/src"), project_dir, &deps);
        assert!(paths.contains(&PathBuf::from("/tmp/test-project/src")));
        assert!(paths.contains(&PathBuf::from("/tmp/mylib")));
    }

    #[test]
    fn test_dep_source_display() {
        let reg = DepSource::Registry { version: "1.0".to_string() };
        assert_eq!(format!("{}", reg), "\"1.0\"");

        let git = DepSource::Git {
            version: "2.0".to_string(),
            url: "https://github.com/user/repo".to_string(),
        };
        assert_eq!(
            format!("{}", git),
            "{ version = \"2.0\", git = \"https://github.com/user/repo\" }"
        );

        let path = DepSource::Path { path: "../lib".to_string() };
        assert_eq!(format!("{}", path), "{ path = \"../lib\" }");
    }

    #[test]
    fn test_collect_hexa_files() {
        let tmp = std::env::temp_dir().join("hexa_collect_test");
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(tmp.join("src")).unwrap();
        fs::write(tmp.join("src/main.hexa"), "println(\"hi\")").unwrap();
        fs::write(tmp.join("src/lib.hexa"), "pub fn x() { 1 }").unwrap();
        fs::write(tmp.join("src/notes.txt"), "not hexa").unwrap();

        let mut files = Vec::new();
        collect_hexa_files(&tmp.join("src"), "src", &mut files);
        assert_eq!(files.len(), 2);
        assert!(files.iter().all(|f| f.ends_with(".hexa")));

        let _ = fs::remove_dir_all(&tmp);
    }
}
