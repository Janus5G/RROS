import datetime as dt
import json
import os
import pathlib
import re
import subprocess
import sys

owner = os.environ['CLEANUP_OWNER']
dry_run = os.environ['DRY_RUN'] == 'true'
min_age = int(os.environ['MIN_AGE_DAYS'])
keep = int(os.environ['KEEP_NEWEST_PER_NAME'])
limit = int(os.environ['MAX_DELETIONS_PER_RUN'])
out = pathlib.Path(os.environ['REPORT_DIR'])
now = dt.datetime.now(dt.timezone.utc)
cutoff = now - dt.timedelta(days=min_age)

# Deliberately narrow allowlist. Never treat arbitrary CI artifacts as disposable.
# This covers cleanup reports created by this workflow; further prefixes require
# explicit review and an intentional script change.
TEMP_PREFIXES = ('tmp-artifact-cleanup-report-',)
PROTECT = re.compile(
    r'release|candidate|distribution|package|signed|signature|provenance|'
    r'security|audit|evidence|hardware|validation|regression|protected',
    re.I,
)

def api(endpoint, method='GET'):
    cmd = ['gh', 'api', '--method', method, '--header', 'Accept: application/vnd.github+json', endpoint]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError as e:
        # Fail closed; do not skip inaccessible repositories/artifact pages silently.
        raise RuntimeError(f'GitHub API failed for {method} {endpoint}: {e.stderr.strip()[:350]}') from e
    return json.loads(result.stdout) if result.stdout.strip() else {}

def paged(endpoint, field):
    page = 1
    items = []
    while True:
        sep = '&' if '?' in endpoint else '?'
        data = api(f'{endpoint}{sep}per_page=100&page={page}')
        part = data.get(field) if isinstance(data, dict) else data
        if not isinstance(part, list):
            raise RuntimeError(f'Unexpected API result for {endpoint}')
        items.extend(part)
        if len(part) < 100:
            return items
        page += 1

def group_name(name):
    # Prefix is a stable group; individual report names end in unique run IDs.
    for prefix in TEMP_PREFIXES:
        if name.startswith(prefix):
            return prefix
    return name

report = {
    'owner': owner,
    'generated_at_utc': now.isoformat(),
    'dry_run': dry_run,
    'minimum_age_days': min_age,
    'keep_newest_per_group': keep,
    'max_deletions_per_run': limit,
    'temporary_name_prefix_allowlist': list(TEMP_PREFIXES),
    'repositories_checked': 0,
    'artifacts_checked': 0,
    'protected_or_unknown_count': 0,
    'candidates': [],
    'deleted': [],
    'errors': [],
}

try:
    repositories = paged('/user/repos?affiliation=owner&visibility=all&sort=full_name', 'unused')
    # Use the authenticated user endpoint so private owned repositories are included.
    repositories = [r for r in repositories if r.get('owner', {}).get('login', '').lower() == owner.lower()]
    if not repositories:
        raise RuntimeError('No owned repositories returned; refuse to treat this as a completed cleanup')
    for repo in repositories:
        full = repo['full_name']
        report['repositories_checked'] += 1
        artifacts = paged(f'/repos/{full}/actions/artifacts', 'artifacts')
        report['artifacts_checked'] += len(artifacts)
        groups = {}
        for a in artifacts:
            name = str(a.get('name', ''))
            if not name.startswith(TEMP_PREFIXES) or PROTECT.search(name):
                report['protected_or_unknown_count'] += 1
                continue
            groups.setdefault(group_name(name), []).append(a)
        for group, records in groups.items():
            records.sort(key=lambda a: (a.get('created_at') or '', a.get('id', 0)), reverse=True)
            for a in records[keep:]:
                when = dt.datetime.fromisoformat(a['created_at'].replace('Z', '+00:00'))
                if when >= cutoff or a.get('expired'):
                    continue
                report['candidates'].append({
                    'repository': full, 'id': a['id'], 'name': a['name'],
                    'created_at': a['created_at'], 'size_in_bytes': a.get('size_in_bytes'),
                    'reason': f'allowlisted temporary group {group}; older than {min_age} days; at least {keep} newest retained',
                })
    report['candidates'].sort(key=lambda a: (a['repository'], a['created_at'], a['id']))
    if not dry_run:
        for entry in report['candidates'][:limit]:
            api(f"/repos/{entry['repository']}/actions/artifacts/{entry['id']}", method='DELETE')
            report['deleted'].append(entry)
except Exception as exc:
    report['errors'].append(str(exc))
finally:
    out.mkdir(parents=True, exist_ok=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    lines = [
        '# Actions artifact cleanup',
        '',
        f"- Mode: {'DRY RUN (no deletion)' if dry_run else 'LIVE'}",
        f"- Repositories checked: {report['repositories_checked']}",
        f"- Artifacts checked: {report['artifacts_checked']}",
        f"- Unknown / protected artifacts: {report['protected_or_unknown_count']}",
        f"- Eligible temporary artifacts: {len(report['candidates'])}",
        f"- Deleted: {len(report['deleted'])}",
        f"- Errors: {len(report['errors'])}",
        '',
        'Only artifacts named `tmp-artifact-cleanup-report-*` are currently eligible.',
        'Other artifacts are deliberately preserved unless a further safe classification is approved.',
    ]
    if report['errors']:
        lines += ['', '## Errors'] + ['- ' + e for e in report['errors']]
