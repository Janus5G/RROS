#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RROS contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and restore the approved GitHub Discussions category configuration.

Default mode (``--check``) validates the local manifest and Discussion forms
offline. ``--repo OWNER/NAME --apply`` additionally enables Issues and
Discussions on an existing, owner-authorized repository, reads the live
categories through GitHub's documented GraphQL ``discussionCategories`` query
and compares them against the manifest.

GitHub's GraphQL schema exposes category reads but no category create/update
mutation, and the CLI repository-edit command only toggles the features.
Missing or mismatched categories are therefore reported with the exact
documented UI steps (Discussions -> Categories -> pencil). This tool never
invents API endpoints, never creates repositories, never pushes and never
deletes existing discussions or categories.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs" / "community" / "discussion_categories.json"
FORM_DIR = ROOT / ".github" / "DISCUSSION_TEMPLATE"
FORMATS = {"announcement", "question-answer", "open-ended", "poll"}

CATEGORY_QUERY = """
query($owner: String!, $name: String!) {
  repository(owner: $owner, name: $name) {
    hasDiscussionsEnabled
    hasIssuesEnabled
    visibility
    discussionCategories(first: 25) {
      nodes { name slug emoji description isAnswerable }
    }
  }
}
"""


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_manifest() -> list[dict]:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    categories = data.get("categories")
    if not isinstance(categories, list) or len(categories) != 8:
        fail("Manifest must list exactly the eight approved categories.")
    slugs: set[str] = set()
    names: set[str] = set()
    for entry in categories:
        for key in ("name", "slug", "emoji", "emoji_code", "format",
                    "is_answerable", "description", "form", "optional"):
            if key not in entry:
                fail(f"Category {entry.get('name', '?')!r} missing key {key!r}.")
        if entry["format"] not in FORMATS:
            fail(f"Category {entry['name']!r} has unknown format {entry['format']!r}.")
        if entry["is_answerable"] != (entry["format"] == "question-answer"):
            fail(f"Category {entry['name']!r} answerable flag contradicts its format.")
        if entry["slug"] in slugs or entry["name"] in names:
            fail(f"Duplicate category name/slug: {entry['name']!r}.")
        slugs.add(entry["slug"])
        names.add(entry["name"])
        if entry["form"] is not None:
            form = ROOT / Path(entry["form"])
            if not form.is_file():
                fail(f"Missing Discussion form: {entry['form']}")
            if form.name != entry["slug"] + ".yml":
                fail(f"Form {entry['form']} is not named after slug {entry['slug']!r}.")
    expected_forms = {ROOT / Path(e["form"]) for e in categories if e["form"]}
    actual_forms = set(FORM_DIR.glob("*.yml"))
    stray = sorted(str(p.relative_to(ROOT)) for p in actual_forms - expected_forms)
    if stray:
        fail(f"Discussion forms not covered by the manifest: {', '.join(stray)}")
    return categories


def run_gh(args: list[str]) -> str:
    result = subprocess.run(["gh", *args], capture_output=True, text=True)
    if result.returncode != 0:
        fail(f"gh {' '.join(args[:2])} failed: {result.stderr.strip()}")
    return result.stdout


def apply_remote(repo: str, categories: list[dict]) -> int:
    owner, _, name = repo.partition("/")
    if not owner or not name or "/" in name:
        fail(f"Invalid repository: {repo!r} (expected OWNER/NAME).")
    run_gh(["repo", "view", repo, "--json", "name"])
    run_gh(["repo", "edit", repo, "--enable-issues", "--enable-discussions"])
    payload = json.loads(run_gh([
        "api", "graphql", "-f", f"query={CATEGORY_QUERY}",
        "-f", f"owner={owner}", "-f", f"name={name}",
    ]))
    repository = payload["data"]["repository"]
    print(f"Repository visibility: {repository['visibility']}")
    print(f"Issues enabled: {repository['hasIssuesEnabled']}, "
          f"Discussions enabled: {repository['hasDiscussionsEnabled']}")
    live = {node["slug"]: node
            for node in repository["discussionCategories"]["nodes"]}
    failures = 0
    for entry in categories:
        node = live.get(entry["slug"])
        if node is None:
            level = "OPTIONAL-MISSING" if entry["optional"] else "MISSING"
            failures += 0 if entry["optional"] else 1
            print(f"{level}: {entry['name']} ({entry['format']})")
            print(f"  UI: Discussions -> Categories -> pencil/new; name "
                  f"{entry['name']!r}, emoji {entry['emoji_code']}, format "
                  f"{entry['format']}, description: {entry['description']}")
            continue
        issues = []
        if node["name"] != entry["name"]:
            issues.append(f"name {node['name']!r} != {entry['name']!r}")
        if node["isAnswerable"] != entry["is_answerable"]:
            issues.append(f"answerable {node['isAnswerable']} != {entry['is_answerable']}")
        if node["description"] != entry["description"]:
            issues.append("description differs")
        if node["emoji"] not in (entry["emoji"], entry["emoji_code"]):
            issues.append(f"emoji {node['emoji']!r} differs")
        if issues:
            failures += 1
            print(f"MISMATCH: {entry['name']}: {'; '.join(issues)}")
            print("  UI: Discussions -> Categories -> pencil; apply the manifest values.")
        else:
            print(f"MATCH: {entry['name']} (slug {entry['slug']})")
        if entry["form"] is not None and node is not None:
            print(f"  Form {entry['form']} maps to live slug {node['slug']!r}; "
                  "open a new discussion in this category to confirm the form loads.")
    unknown = sorted(set(live) - {e["slug"] for e in categories})
    if unknown:
        print(f"NOTE: extra live categories left untouched: {', '.join(unknown)}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="Offline manifest/form validation only (default).")
    parser.add_argument("--repo", help="Existing authorized repository (OWNER/NAME).")
    parser.add_argument("--apply", action="store_true",
                        help="Enable features and verify live categories; requires --repo "
                             "and prior owner authorization.")
    args = parser.parse_args()
    categories = load_manifest()
    print(f"Manifest OK: eight approved categories, forms present and slug-named.")
    if not args.apply:
        print("Offline check complete. Live restoration requires --repo and --apply "
              "after the owner creates and authorizes the repository.")
        return 0
    if not args.repo:
        fail("--apply requires --repo OWNER/NAME.")
    failures = apply_remote(args.repo, categories)
    if failures:
        print(f"RESTORATION INCOMPLETE: {failures} required categories need the "
              "documented UI step above.")
        return 1
    print("All required categories match the approved configuration.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
