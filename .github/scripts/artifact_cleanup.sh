#!/usr/bin/env bash
# Account-wide GitHub Actions artifact cleanup for repositories owned by a
# single user. Uses only the supported REST API through the GitHub CLI.
#
# Safety model:
#   * Explicit allow/deny classification. Artifacts matching PROTECTED
#     patterns are never deleted. Only artifacts matching explicit TEMPORARY
#     patterns are deletion candidates. Everything else is UNKNOWN and is
#     reported, never deleted.
#   * Temporary artifacts are deleted only when older than MIN_AGE_DAYS and
#     never the newest KEEP_NEWEST_PER_NAME artifacts of each name per repo.
#   * DRY_RUN=true (the default) produces the full deletion plan without
#     calling any DELETE endpoint.
#   * Never touches workflow runs, releases, git history or repository files.
set -euo pipefail

OWNER="${CLEANUP_OWNER:-Janus5G}"
DRY_RUN="${DRY_RUN:-true}"
MIN_AGE_DAYS="${MIN_AGE_DAYS:-14}"
KEEP_NEWEST_PER_NAME="${KEEP_NEWEST_PER_NAME:-2}"
MAX_DELETIONS_PER_RUN="${MAX_DELETIONS_PER_RUN:-200}"
REPORT_DIR="${REPORT_DIR:-build/artifact-cleanup}"
SUMMARY_FILE="${GITHUB_STEP_SUMMARY:-${REPORT_DIR}/summary.md}"
export OWNER

if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: GitHub CLI (gh) is required." >&2
    exit 1
fi
if [ -z "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ] && ! gh auth status >/dev/null 2>&1; then
    echo "ERROR: no GitHub credentials available (GH_TOKEN unset, gh not logged in)." >&2
    exit 1
fi

mkdir -p "$REPORT_DIR"
INVENTORY="$REPORT_DIR/inventory.tsv"       # every non-expired artifact seen
PLAN_DELETE="$REPORT_DIR/plan-delete.tsv"   # complete deletion plan
PRESERVED="$REPORT_DIR/preserved.tsv"       # kept artifacts with reasons
UNKNOWN="$REPORT_DIR/unknown.tsv"           # unclassifiable, reported only
ERRORS="$REPORT_DIR/errors.log"
RESULTS="$REPORT_DIR/deletion-results.tsv"
HEADER=$'repo\tartifact_name\tartifact_id\tsize_bytes\tcreated_at\treason'
printf '%s\n' "$HEADER" | tee "$PLAN_DELETE" >"$PRESERVED"
printf '%s\n' "$HEADER" >"$UNKNOWN"
printf 'repo\tartifact_name\tartifact_id\tsize_bytes\tcreated_at\tclass\n' >"$INVENTORY"
printf 'repo\tartifact_id\toutcome\n' >"$RESULTS"
: >"$ERRORS"

NOW_EPOCH="$(date -u +%s)"
CUTOFF_EPOCH=$((NOW_EPOCH - MIN_AGE_DAYS * 86400))

# ---------------------------------------------------------------------------
# Classification. Checked in order; first match wins. Protection always wins
# over temporary patterns. Names are lowercased before matching.
# ---------------------------------------------------------------------------
classify() {
    local n="$1"
    case "$n" in
        # Explicit preservation markers.
        *do-not-delete*|*preserve*|*keep*|*retain*|*archive*)
            echo "protected:explicitly-marked" ;;
        # Release candidates and approved distribution packages.
        *release*|*candidate*|*-rc.*|*-rc-*|*rc[0-9]*|*community*|*dist-*|*-dist*|*package*)
            echo "protected:release-or-distribution" ;;
        # Signed artifacts and provenance records.
        *signed*|*signature*|*signing*|*provenance*|*attestation*|*sbom*|*slsa*|*cosign*)
            echo "protected:signing-or-provenance" ;;
        # Security audit evidence (including scanner/SARIF output).
        *security*|*audit*|*static-analysis*|*sanitizer*|*cve*|*vuln*|*fuzz*|*sarif*|*gitleaks*|*codeql*)
            echo "protected:security-evidence" ;;
        # Hardware validation reports.
        *hw-validation*|*hw_validation*|*hardware*|*pi5*|*board-*|*-board*|*serial-capture*|*validation*|*verification*)
            echo "protected:hardware-validation" ;;
        # Regression-test and gate acceptance evidence (incl. CR01..CR99).
        *evidence*|*regression*|*cr[0-9][0-9]*|*gate*|*acceptance*|*conformance*)
            echo "protected:regression-evidence" ;;
        # Ordinary temporary CI artifacts (explicit allow-list for deletion).
        *tmp*|*temp-*|*-temp*|*temporary*|*cache*|*scratch*|*intermediate*|*work-in-progress*|*wip-*)
            echo "temporary" ;;
        *debug-log*|*-logs*|*logs-*|*build-log*|*console-output*|*stdout*|*trace-*)
            echo "temporary" ;;
        *coverage*|*lint*|*junit*|*test-output*|*test-results*|*benchmark*|*profiling*)
            echo "temporary" ;;
        *nightly*|*snapshot*|*ci-build*|*draft-*|*throwaway*)
            echo "temporary" ;;
        # Anything else is unknown: report, never delete.
        *)
            echo "unknown" ;;
    esac
}

# ---------------------------------------------------------------------------
# Rate-limit awareness: pause when the core budget runs low.
# ---------------------------------------------------------------------------
check_rate_limit() {
    local remaining reset wait_s
    remaining="$(gh api rate_limit --jq '.resources.core.remaining' 2>>"$ERRORS")" || return 0
    if [ "${remaining:-0}" -lt 100 ]; then
        reset="$(gh api rate_limit --jq '.resources.core.reset' 2>>"$ERRORS")" || return 0
        wait_s=$((reset - $(date -u +%s) + 5))
        [ "$wait_s" -lt 5 ] && wait_s=5
        echo "Rate limit low (${remaining} remaining); sleeping ${wait_s}s" >&2
        sleep "$wait_s"
    fi
}

# ---------------------------------------------------------------------------
# Enumerate owned repositories (paginated). Archived repos are inspected but
# skipped for deletion because the API rejects writes to them.
# ---------------------------------------------------------------------------
echo "Enumerating repositories owned by ${OWNER}..." >&2
REPO_LIST="$REPORT_DIR/.repos.tsv"
if ! gh api --paginate 'user/repos?per_page=100&affiliation=owner' \
        --jq '.[] | select(.owner.login == env.OWNER) | [.name, (.archived|tostring)] | @tsv' \
        >"$REPO_LIST" 2>>"$ERRORS"; then
    echo "ERROR: unable to list repositories for ${OWNER}; see ${ERRORS}" >&2
    exit 1
fi

REPOS_INSPECTED=0
TOTAL_FOUND=0
TOTAL_FOUND_BYTES=0
DELETE_COUNT=0
DELETE_BYTES=0
PRESERVE_COUNT=0
UNKNOWN_COUNT=0
REPO_ERRORS=0

while IFS=$'\t' read -r repo archived; do
    [ -n "$repo" ] || continue
    check_rate_limit
    REPOS_INSPECTED=$((REPOS_INSPECTED + 1))
    echo "Inspecting ${OWNER}/${repo} (archived=${archived})..." >&2

    ART_LIST="$REPORT_DIR/.artifacts.tsv"
    # Expired artifacts no longer consume storage and cannot be acted on.
    if ! gh api --paginate "repos/${OWNER}/${repo}/actions/artifacts?per_page=100" \
            --jq '.artifacts[] | select(.expired == false) | [(.id|tostring), (.size_in_bytes|tostring), .created_at, (.name|@base64)] | @tsv' \
            >"$ART_LIST" 2>>"$ERRORS"; then
        echo "WARN: could not list artifacts in ${OWNER}/${repo}; skipping repo." | tee -a "$ERRORS" >&2
        REPO_ERRORS=$((REPO_ERRORS + 1))
        continue
    fi

    # Build per-repo classified inventory: class, name, created_epoch, id, size, created_at
    CLASSIFIED="$REPORT_DIR/.classified.tsv"
    : >"$CLASSIFIED"
    while IFS=$'\t' read -r id size created name_b64; do
        [ -n "$id" ] || continue
        name="$(printf '%s' "$name_b64" | base64 -d)"
        name_lc="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
        cls="$(classify "$name_lc")"
        if ! created_epoch="$(date -u -d "$created" +%s 2>>"$ERRORS")"; then
            echo "WARN: unparsable created_at '${created}' for ${repo}/${name} (#${id}); treating as unknown." >>"$ERRORS"
            cls="unknown"
            created_epoch="$NOW_EPOCH"
        fi
        TOTAL_FOUND=$((TOTAL_FOUND + 1))
        TOTAL_FOUND_BYTES=$((TOTAL_FOUND_BYTES + size))
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$cls" "$name" "$created_epoch" "$id" "$size" "$created" >>"$CLASSIFIED"
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "$cls" >>"$INVENTORY"
    done <"$ART_LIST"

    # Protected and unknown artifacts are always preserved.
    while IFS=$'\t' read -r cls name created_epoch id size created; do
        case "$cls" in
            protected:*)
                printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "$cls" >>"$PRESERVED"
                PRESERVE_COUNT=$((PRESERVE_COUNT + 1)) ;;
            unknown)
                printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "unknown-purpose:reported-not-deleted" >>"$UNKNOWN"
                printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "unknown-purpose" >>"$PRESERVED"
                UNKNOWN_COUNT=$((UNKNOWN_COUNT + 1))
                PRESERVE_COUNT=$((PRESERVE_COUNT + 1)) ;;
        esac
    done <"$CLASSIFIED"

    # Temporary artifacts: newest KEEP_NEWEST_PER_NAME per name are always
    # kept; older ones must also exceed MIN_AGE_DAYS to become candidates.
    awk -F '\t' '$1 == "temporary"' "$CLASSIFIED" \
        | sort -t $'\t' -k2,2 -k3,3nr \
        | awk -F '\t' -v OFS='\t' '{ seq[$2]++; print seq[$2], $0 }' \
        >"$REPORT_DIR/.temp-ranked.tsv"

    while IFS=$'\t' read -r rank cls name created_epoch id size created; do
        [ -n "$id" ] || continue
        if [ "$rank" -le "$KEEP_NEWEST_PER_NAME" ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "temporary:newest-${KEEP_NEWEST_PER_NAME}-of-name" >>"$PRESERVED"
            PRESERVE_COUNT=$((PRESERVE_COUNT + 1))
        elif [ "$created_epoch" -gt "$CUTOFF_EPOCH" ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "temporary:younger-than-${MIN_AGE_DAYS}d" >>"$PRESERVED"
            PRESERVE_COUNT=$((PRESERVE_COUNT + 1))
        elif [ "$archived" = "true" ]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "temporary:repo-archived-read-only" >>"$PRESERVED"
            PRESERVE_COUNT=$((PRESERVE_COUNT + 1))
        else
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$repo" "$name" "$id" "$size" "$created" "temporary:older-than-${MIN_AGE_DAYS}d-beyond-newest-${KEEP_NEWEST_PER_NAME}" >>"$PLAN_DELETE"
            DELETE_COUNT=$((DELETE_COUNT + 1))
            DELETE_BYTES=$((DELETE_BYTES + size))
        fi
    done <"$REPORT_DIR/.temp-ranked.tsv"
done <"$REPO_LIST"

# ---------------------------------------------------------------------------
# Execute deletions (live mode only). Each DELETE is independent; failures
# are recorded and never abort the run.
# ---------------------------------------------------------------------------
DELETED_OK=0
DELETED_FAIL=0
DELETED_BYTES=0
DELETE_SKIPPED_CAP=0
if [ "$DRY_RUN" != "true" ]; then
    while IFS=$'\t' read -r repo name id size created reason; do
        [ "$repo" = "repo" ] && continue
        [ -n "$id" ] || continue
        if [ $((DELETED_OK + DELETED_FAIL)) -ge "$MAX_DELETIONS_PER_RUN" ]; then
            DELETE_SKIPPED_CAP=$((DELETE_SKIPPED_CAP + 1))
            continue
        fi
        check_rate_limit
        if gh api -X DELETE "repos/${OWNER}/${repo}/actions/artifacts/${id}" >/dev/null 2>>"$ERRORS"; then
            printf '%s\t%s\tdeleted\n' "$repo" "$id" >>"$RESULTS"
            DELETED_OK=$((DELETED_OK + 1))
            DELETED_BYTES=$((DELETED_BYTES + size))
        else
            printf '%s\t%s\tFAILED\n' "$repo" "$id" >>"$RESULTS"
            echo "ERROR: failed to delete artifact ${id} (${name}) in ${OWNER}/${repo}" >>"$ERRORS"
            DELETED_FAIL=$((DELETED_FAIL + 1))
        fi
    done <"$PLAN_DELETE"
fi

rm -f "$REPORT_DIR/.repos.tsv" "$REPORT_DIR/.artifacts.tsv" "$REPORT_DIR/.classified.tsv" "$REPORT_DIR/.temp-ranked.tsv"

# ---------------------------------------------------------------------------
# Summary.
# ---------------------------------------------------------------------------
mib() { awk -v b="$1" 'BEGIN { printf "%.1f MiB", b / 1048576 }'; }
MODE_LABEL="DRY RUN (no deletions performed)"
[ "$DRY_RUN" != "true" ] && MODE_LABEL="LIVE (deletions executed)"

{
    echo "# Artifact cleanup summary — ${MODE_LABEL}"
    echo
    echo "| Metric | Value |"
    echo "| --- | --- |"
    echo "| Repositories inspected | ${REPOS_INSPECTED} |"
    echo "| Repositories with listing errors | ${REPO_ERRORS} |"
    echo "| Artifacts found (non-expired) | ${TOTAL_FOUND} ($(mib "$TOTAL_FOUND_BYTES")) |"
    echo "| Artifacts selected for deletion | ${DELETE_COUNT} |"
    echo "| Artifacts preserved | ${PRESERVE_COUNT} |"
    echo "| Unknown artifacts (reported, not deleted) | ${UNKNOWN_COUNT} |"
    echo "| Total bytes eligible for recovery | ${DELETE_BYTES} ($(mib "$DELETE_BYTES")) |"
    if [ "$DRY_RUN" != "true" ]; then
        echo "| Deletions succeeded | ${DELETED_OK} ($(mib "$DELETED_BYTES") freed) |"
        echo "| Deletions failed | ${DELETED_FAIL} |"
        echo "| Deferred by per-run cap (${MAX_DELETIONS_PER_RUN}) | ${DELETE_SKIPPED_CAP} |"
    fi
    echo
    echo "Policy: delete only explicit temporary CI artifacts older than ${MIN_AGE_DAYS} days,"
    echo "always keeping the newest ${KEEP_NEWEST_PER_NAME} of each artifact name per repository."
    echo "Protected classes (never deleted): release/distribution, signed/provenance,"
    echo "security audit evidence, hardware validation, regression evidence, explicit markers."
    echo
    if [ "$DELETE_COUNT" -gt 0 ]; then
        echo "## Deletion plan"
        echo
        echo '| Repo | Artifact | ID | Size | Created | Reason |'
        echo '| --- | --- | --- | --- | --- | --- |'
        tail -n +2 "$PLAN_DELETE" | head -50 | awk -F '\t' '{ printf "| %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $5, $6 }'
        [ "$DELETE_COUNT" -gt 50 ] && echo "... ${DELETE_COUNT} total; full plan in plan-delete.tsv"
        echo
    fi
    if [ "$UNKNOWN_COUNT" -gt 0 ]; then
        echo "## Unknown artifacts requiring owner review (NOT deleted)"
        echo
        echo '| Repo | Artifact | ID | Size | Created |'
        echo '| --- | --- | --- | --- | --- |'
        tail -n +2 "$UNKNOWN" | head -50 | awk -F '\t' '{ printf "| %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $5 }'
        echo
    fi
    if [ -s "$ERRORS" ]; then
        echo "## Errors"
        echo
        echo '```'
        head -50 "$ERRORS"
        echo '```'
    fi
} >>"$SUMMARY_FILE"

echo "Done. Mode: ${MODE_LABEL}. Found ${TOTAL_FOUND}, plan ${DELETE_COUNT} ($(mib "$DELETE_BYTES")), preserved ${PRESERVE_COUNT}, unknown ${UNKNOWN_COUNT}." >&2
echo "Reports in ${REPORT_DIR}/" >&2
