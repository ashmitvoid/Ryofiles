#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

SCRIPT_VERSION="1"
DEFAULT_ROOT="${PWD}/ryofiles-validation"
ROOT="${RYOFILES_VALIDATION_ROOT:-$DEFAULT_ROOT}"
RESULTS_DIR="${ROOT}/results"
FIXTURES_DIR="${ROOT}/fixtures"
ENV_FILE="${ROOT}/environment.md"
REPORT_FILE="${ROOT}/REPORT.md"
RYOFILES_BIN="${RYOFILES_BIN:-$(command -v ryofiles 2>/dev/null || true)}"
HELPER_BIN="${RYOFILES_PREVIEW_HELPER_BIN:-$(command -v ryofiles-preview-helper 2>/dev/null || true)}"
export RYOFILES_BIN HELPER_BIN

log() {
    printf '[ryofiles-validation] %s\n' "$*"
}

die() {
    printf '[ryofiles-validation] ERROR: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Ryofiles V1.1 target-machine validation collector

Usage:
  bash tools/validation/target-machine.sh start [fixture_count]
  bash tools/validation/target-machine.sh fixtures [count]
  bash tools/validation/target-machine.sh snapshot LABEL
  bash tools/validation/target-machine.sh sample LABEL [seconds] [interval]
  bash tools/validation/target-machine.sh idle-expiry [timeout_seconds]
  bash tools/validation/target-machine.sh report
  bash tools/validation/target-machine.sh self-test

Environment:
  RYOFILES_VALIDATION_ROOT      Evidence directory (default: ./ryofiles-validation)
  RYOFILES_BIN                  Ryofiles binary to validate (default: command -v ryofiles)
  RYOFILES_PREVIEW_HELPER_BIN   Helper binary (default: command -v ryofiles-preview-helper)

The collector never changes Ryofiles configuration. It creates files only beneath
RYOFILES_VALIDATION_ROOT.
EOF
}

ensure_dirs() {
    mkdir -p "$RESULTS_DIR" "$FIXTURES_DIR"
}

command_block() {
    local title="$1"
    shift
    {
        printf '\n### %s\n\n```text\n' "$title"
        "$@" 2>&1 || true
        printf '```\n'
    } >> "$ENV_FILE"
}

# /proc/<pid>/comm is limited to 15 bytes on Linux, which truncates
# "ryofiles-preview-helper". Match the executable basename instead.
pids_for_name() {
    local name="$1"
    local proc pid exe
    for proc in /proc/[0-9]*; do
        [[ -e "$proc/exe" ]] || continue
        pid="${proc##*/}"
        exe="$(readlink "$proc/exe" 2>/dev/null || true)"
        [[ -n "$exe" ]] || continue
        exe="${exe% (deleted)}"
        if [[ "${exe##*/}" == "$name" ]]; then
            printf '%s\n' "$pid"
        fi
    done
}

sum_ps_field() {
    local field="$1"
    shift
    local pids=("$@")
    if ((${#pids[@]} == 0)); then
        printf '0\n'
        return
    fi

    local total="0"
    local pid value
    for pid in "${pids[@]}"; do
        value="$(ps -p "$pid" -o "${field}=" 2>/dev/null | tr -d ' ' || true)"
        [[ -n "$value" ]] || continue
        total="$(awk -v a="$total" -v b="$value" 'BEGIN { printf "%.2f", a + b }')"
    done
    printf '%s\n' "$total"
}

sum_pss_kib() {
    local pids=("$@")
    local total=0
    local pid value
    for pid in "${pids[@]}"; do
        [[ -r "/proc/${pid}/smaps_rollup" ]] || continue
        value="$(awk '/^Pss:/ { print $2; exit }' "/proc/${pid}/smaps_rollup" 2>/dev/null || true)"
        [[ "$value" =~ ^[0-9]+$ ]] || continue
        total=$((total + value))
    done
    printf '%s\n' "$total"
}

sum_cpu_ticks() {
    local pids=("$@")
    local total=0
    local pid value
    for pid in "${pids[@]}"; do
        [[ -r "/proc/${pid}/stat" ]] || continue
        value="$(awk '{ print $14 + $15 }' "/proc/${pid}/stat" 2>/dev/null || true)"
        [[ "$value" =~ ^[0-9]+$ ]] || continue
        total=$((total + value))
    done
    printf '%s\n' "$total"
}

interval_cpu_percent() {
    local previous_ticks="$1"
    local current_ticks="$2"
    local previous_ms="$3"
    local current_ms="$4"
    local clk_tck="$5"
    local delta_ticks=$((current_ticks - previous_ticks))
    local delta_ms=$((current_ms - previous_ms))
    if ((delta_ticks < 0 || delta_ms <= 0)); then
        printf '0.00\n'
        return
    fi
    awk -v ticks="$delta_ticks" -v ms="$delta_ms" -v hz="$clk_tck" \
        'BEGIN { printf "%.2f", (ticks * 100000.0) / (hz * ms) }'
}

capture_environment() {
    ensure_dirs
    : > "$ENV_FILE"
    {
        printf '# Ryofiles V1.1 target-machine environment\n\n'
        printf -- '- Collector version: `%s`\n' "$SCRIPT_VERSION"
        printf -- '- Captured: `%s`\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
        printf -- '- Evidence root: `%s`\n' "$ROOT"
        printf -- '- Ryofiles binary: `%s`\n' "${RYOFILES_BIN:-not found}"
        printf -- '- Preview helper: `%s`\n' "${HELPER_BIN:-not found}"
    } >> "$ENV_FILE"

    command_block "Kernel" uname -a
    command_block "OS release" cat /etc/os-release
    command_block "Ryofiles version" bash -c 'if [[ -n "${RYOFILES_BIN:-}" && -x "$RYOFILES_BIN" ]]; then "$RYOFILES_BIN" --version; else echo "Ryofiles binary not found"; fi'
    command_block "Installed Ryofiles packages" bash -c 'if command -v pacman >/dev/null; then pacman -Q | grep -E "^ryofiles($|-git )" || true; else echo "pacman unavailable"; fi'
    command_block "Repository state" bash -c 'if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then printf "HEAD="; git rev-parse HEAD; git status --short --branch; else echo "not inside a Git work tree"; fi'
    command_block "Hyprland version" bash -c 'command -v hyprctl >/dev/null && hyprctl version || echo "hyprctl unavailable"'
    command_block "Hyprland monitors" bash -c 'command -v hyprctl >/dev/null && hyprctl monitors || echo "hyprctl unavailable"'
    command_block "Hyprland active workspace" bash -c 'command -v hyprctl >/dev/null && hyprctl activeworkspace || echo "hyprctl unavailable"'
    command_block "Session" bash -c 'printf "XDG_SESSION_TYPE=%s\nXDG_CURRENT_DESKTOP=%s\nWAYLAND_DISPLAY=%s\nQT_SCALE_FACTOR=%s\n" "${XDG_SESSION_TYPE:-}" "${XDG_CURRENT_DESKTOP:-}" "${WAYLAND_DISPLAY:-}" "${QT_SCALE_FACTOR:-}"'
    command_block "Ryofiles runtime linkage" bash -c 'if [[ -n "${RYOFILES_BIN:-}" && -x "$RYOFILES_BIN" ]]; then ldd "$RYOFILES_BIN"; else echo "Ryofiles binary not found"; fi'
    command_block "Preview-helper runtime linkage" bash -c 'if [[ -n "${HELPER_BIN:-}" && -x "$HELPER_BIN" ]]; then ldd "$HELPER_BIN"; else echo "Preview helper not found"; fi'
    command_block "Mounted filesystems" findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS

    log "environment captured in $ENV_FILE"
}

create_fixtures() {
    ensure_dirs
    local count="${1:-10000}"
    [[ "$count" =~ ^[0-9]+$ ]] || die "fixture count must be an integer"
    ((count >= 1)) || die "fixture count must be at least 1"

    rm -rf "$FIXTURES_DIR"
    mkdir -p "$FIXTURES_DIR/large-directory" "$FIXTURES_DIR/mixed"

    printf 'regular preview fixture\n' > "$FIXTURES_DIR/mixed/regular.txt"
    printf 'not a pdf\n' > "$FIXTURES_DIR/mixed/corrupt.pdf"
    printf 'not an image\n' > "$FIXTURES_DIR/mixed/corrupt.png"
    printf 'not a font\n' > "$FIXTURES_DIR/mixed/corrupt.ttf"
    printf 'not a media container\n' > "$FIXTURES_DIR/mixed/corrupt.mp4"
    printf 'not a gif\n' > "$FIXTURES_DIR/mixed/corrupt.gif"
    printf 'not a webp\n' > "$FIXTURES_DIR/mixed/corrupt.webp"
    printf 'unicode\n' > "$FIXTURES_DIR/mixed/नमस्ते-文件-🎵.txt"

    local long_name
    long_name="$(printf 'a%.0s' {1..220}).txt"
    printf 'long filename\n' > "$FIXTURES_DIR/mixed/$long_name"

    ln -s "regular.txt" "$FIXTURES_DIR/mixed/link-to-regular.txt"
    ln -s "missing-target" "$FIXTURES_DIR/mixed/broken-link.txt"

    # Sparse invalid inputs exercise input-size rejection without writing large payloads.
    truncate -s 70M "$FIXTURES_DIR/mixed/oversized-animation.gif"
    truncate -s 40M "$FIXTURES_DIR/mixed/oversized-font.ttf"
    truncate -s 70M "$FIXTURES_DIR/mixed/oversized-media.mp4"

    local i
    for ((i = 1; i <= count; ++i)); do
        : > "$FIXTURES_DIR/large-directory/file-$(printf '%06d' "$i").txt"
    done

    cat > "$FIXTURES_DIR/README.txt" <<EOF
Ryofiles target-machine fixtures

large-directory/ contains ${count} empty files for listing/selection stress.
mixed/ contains corrupt, oversized sparse, Unicode, long-name, symlink and broken-link cases.
All files are disposable and exist only beneath:
${FIXTURES_DIR}
EOF

    log "created fixtures in $FIXTURES_DIR"
}

snapshot() {
    ensure_dirs
    local label="${1:-snapshot}"
    local safe_label
    safe_label="$(printf '%s' "$label" | tr -cs 'A-Za-z0-9._-' '_')"
    local out="$RESULTS_DIR/${safe_label}-snapshot.txt"

    local app_pids=()
    local helper_pids=()
    mapfile -t app_pids < <(pids_for_name ryofiles)
    mapfile -t helper_pids < <(pids_for_name ryofiles-preview-helper)

    {
        printf 'timestamp=%s\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
        printf 'label=%s\n' "$label"
        printf 'app_count=%d\n' "${#app_pids[@]}"
        printf 'helper_count=%d\n' "${#helper_pids[@]}"
        printf 'app_pids=%s\n' "${app_pids[*]:-}"
        printf 'helper_pids=%s\n' "${helper_pids[*]:-}"
        printf 'app_rss_kib=%s\n' "$(sum_ps_field rss "${app_pids[@]}")"
        printf 'app_pss_kib=%s\n' "$(sum_pss_kib "${app_pids[@]}")"
        printf 'app_cpu_lifetime_percent=%s\n' "$(sum_ps_field %cpu "${app_pids[@]}")"
        printf 'helper_rss_kib=%s\n' "$(sum_ps_field rss "${helper_pids[@]}")"
        printf 'helper_pss_kib=%s\n' "$(sum_pss_kib "${helper_pids[@]}")"
        printf 'helper_cpu_lifetime_percent=%s\n' "$(sum_ps_field %cpu "${helper_pids[@]}")"
    } > "$out"

    cat "$out"
    log "snapshot saved to $out"
}

sample_processes() {
    ensure_dirs
    local label="${1:-sample}"
    local seconds="${2:-60}"
    local interval="${3:-1}"
    [[ "$seconds" =~ ^[0-9]+$ ]] || die "seconds must be an integer"
    [[ "$interval" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "interval must be numeric"
    ((seconds >= 1)) || die "seconds must be at least 1"

    local safe_label
    safe_label="$(printf '%s' "$label" | tr -cs 'A-Za-z0-9._-' '_')"
    local out="$RESULTS_DIR/${safe_label}.csv"
    printf 'elapsed_s,timestamp,app_count,helper_count,app_rss_kib,app_pss_kib,app_cpu_interval_percent,helper_rss_kib,helper_pss_kib,helper_cpu_interval_percent\n' > "$out"

    local clk_tck
    clk_tck="$(getconf CLK_TCK)"
    [[ "$clk_tck" =~ ^[0-9]+$ ]] || die "could not determine CLK_TCK"

    local start_ms previous_ms now_ms elapsed_ms max_helpers=0
    local previous_app_ticks=0 previous_helper_ticks=0
    local app_seen=0 first=1
    start_ms="$(date +%s%3N)"
    previous_ms="$start_ms"

    while true; do
        now_ms="$(date +%s%3N)"
        elapsed_ms=$((now_ms - start_ms))
        ((elapsed_ms <= seconds * 1000)) || break

        local app_pids=()
        local helper_pids=()
        mapfile -t app_pids < <(pids_for_name ryofiles)
        mapfile -t helper_pids < <(pids_for_name ryofiles-preview-helper)
        if ((${#app_pids[@]} > 0)); then
            app_seen=1
        fi
        if ((${#helper_pids[@]} > max_helpers)); then
            max_helpers=${#helper_pids[@]}
        fi

        local app_ticks helper_ticks app_cpu helper_cpu elapsed_seconds
        app_ticks="$(sum_cpu_ticks "${app_pids[@]}")"
        helper_ticks="$(sum_cpu_ticks "${helper_pids[@]}")"
        if ((first)); then
            app_cpu="0.00"
            helper_cpu="0.00"
            first=0
        else
            app_cpu="$(interval_cpu_percent "$previous_app_ticks" "$app_ticks" "$previous_ms" "$now_ms" "$clk_tck")"
            helper_cpu="$(interval_cpu_percent "$previous_helper_ticks" "$helper_ticks" "$previous_ms" "$now_ms" "$clk_tck")"
        fi
        elapsed_seconds="$(awk -v ms="$elapsed_ms" 'BEGIN { printf "%.3f", ms / 1000.0 }')"

        printf '%s,%s,%d,%d,%s,%s,%s,%s,%s,%s\n' \
            "$elapsed_seconds" \
            "$(date --iso-8601=seconds 2>/dev/null || date)" \
            "${#app_pids[@]}" \
            "${#helper_pids[@]}" \
            "$(sum_ps_field rss "${app_pids[@]}")" \
            "$(sum_pss_kib "${app_pids[@]}")" \
            "$app_cpu" \
            "$(sum_ps_field rss "${helper_pids[@]}")" \
            "$(sum_pss_kib "${helper_pids[@]}")" \
            "$helper_cpu" >> "$out"

        previous_app_ticks="$app_ticks"
        previous_helper_ticks="$helper_ticks"
        previous_ms="$now_ms"
        sleep "$interval"
    done

    local status="PASS"
    local reason="helper count remained within the process-wide <=2 invariant"
    if ((max_helpers > 2)); then
        status="FAIL"
        reason="observed ${max_helpers} preview helpers; process-wide invariant is <=2"
    elif ((app_seen == 0)); then
        status="INCOMPLETE"
        reason="Ryofiles was never observed running during the sample"
    fi

    printf '%s\t%s\t%s\n' "$status" "$label" "$reason" >> "$RESULTS_DIR/gates.tsv"
    log "$status: $label — $reason"
    log "sample saved to $out"
    [[ "$status" != "FAIL" ]]
}

idle_expiry() {
    ensure_dirs
    local timeout="${1:-15}"
    [[ "$timeout" =~ ^[0-9]+$ ]] || die "timeout must be an integer"
    ((timeout >= 1)) || die "timeout must be at least 1"

    local helpers=()
    mapfile -t helpers < <(pids_for_name ryofiles-preview-helper)
    if ((${#helpers[@]} == 0)); then
        printf 'INCOMPLETE\thelper-idle-expiry\tNo helper was running at test start; trigger one bounded preview request first\n' >> "$RESULTS_DIR/gates.tsv"
        die "no preview helper is running; trigger a preview request, then run idle-expiry immediately"
    fi

    local start_ms now_ms elapsed_ms zero_since=-1
    start_ms="$(date +%s%3N)"
    while true; do
        now_ms="$(date +%s%3N)"
        elapsed_ms=$((now_ms - start_ms))
        mapfile -t helpers < <(pids_for_name ryofiles-preview-helper)

        if ((${#helpers[@]} == 0)); then
            if ((zero_since < 0)); then
                zero_since=$elapsed_ms
            fi
            if ((elapsed_ms - zero_since >= 1000)); then
                local elapsed_seconds
                elapsed_seconds="$(awk -v ms="$zero_since" 'BEGIN { printf "%.3f", ms / 1000.0 }')"
                printf 'PASS\thelper-idle-expiry\tHelper exited after %ss and stayed absent for >=1s\n' "$elapsed_seconds" >> "$RESULTS_DIR/gates.tsv"
                printf 'status=PASS\nelapsed_seconds=%s\n' "$elapsed_seconds" > "$RESULTS_DIR/helper-idle-expiry.txt"
                log "PASS: helper idle expiry observed after ${elapsed_seconds}s"
                return
            fi
        else
            zero_since=-1
            if ((${#helpers[@]} > 2)); then
                printf 'FAIL\thelper-idle-expiry\tObserved %d helpers; process-wide invariant is <=2\n' "${#helpers[@]}" >> "$RESULTS_DIR/gates.tsv"
                printf 'status=FAIL\nreason=helper-count\n' > "$RESULTS_DIR/helper-idle-expiry.txt"
                die "observed more than two preview helpers"
            fi
        fi

        if ((elapsed_ms >= timeout * 1000)); then
            printf 'FAIL\thelper-idle-expiry\tHelper remained present beyond %ss timeout\n' "$timeout" >> "$RESULTS_DIR/gates.tsv"
            printf 'status=FAIL\ntimeout_seconds=%s\n' "$timeout" > "$RESULTS_DIR/helper-idle-expiry.txt"
            die "preview helper remained present beyond ${timeout}s"
        fi
        sleep 0.25
    done
}

write_report() {
    ensure_dirs
    local gate_file="$RESULTS_DIR/gates.tsv"
    local gate_lines=""
    if [[ -f "$gate_file" ]]; then
        gate_lines="$(cat "$gate_file")"
    fi

    {
        printf '# Ryofiles V1.1 target-machine validation report\n\n'
        printf -- '- Generated: `%s`\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
        printf -- '- Evidence root: `%s`\n' "$ROOT"
        printf -- '- Collector version: `%s`\n\n' "$SCRIPT_VERSION"

        printf '## Automated observations\n\n'
        if [[ -n "$gate_lines" ]]; then
            printf '| Status | Gate | Evidence |\n|---|---|---|\n'
            while IFS=$'\t' read -r status gate evidence; do
                [[ -n "$status" ]] || continue
                printf '| %s | %s | %s |\n' "$status" "$gate" "${evidence//|/\\|}"
            done <<< "$gate_lines"
        else
            printf '_No automated gate observations recorded yet._\n'
        fi

        cat <<'EOF'

## Required manual gates

Mark each item only after testing the exact candidate build recorded in `environment.md`.

- [ ] Real Ryoku/Hyprland session: main window opens, focuses, resizes and closes normally.
- [ ] Rapid selection: scrub repeatedly across image/PDF/media/font/corrupt fixtures; newest selection wins, no stale preview replaces it, no crash/hang.
- [ ] Image UX: fit/zoom/pan behave correctly; GIF/WebP animation starts only on explicit action and stops on close/selection change/reduced motion.
- [ ] Media: metadata/poster appear; playback never autostarts; Play/Pause/Stop/seek work; selection change releases playback.
- [ ] PDF: page navigation and metadata work; corrupt/oversized inputs fail cleanly.
- [ ] Font: metadata/sample work; corrupt/oversized inputs fail cleanly.
- [ ] Properties: rich metadata matches PreviewPanel sources; opening folder Properties does not recursively size the directory until Calculate Size is pressed.
- [ ] Theme: change Ryoku theme while Ryofiles is open; tokens update without restart or stale colors.
- [ ] Reduced motion: enable reduced motion; bounded animation stops/does not continue invisibly and UI remains usable.
- [ ] HiDPI/per-monitor scale: verify text, hit targets, previews and picker layout at every target monitor scale.
- [ ] Removable storage: browse/copy/preview on a real removable device; unplug/remount failures remain bounded and recoverable.
- [ ] Slow storage: repeat listing/preview/cancel operations on genuinely slow media; UI thread remains responsive and stale work is cancelled.
- [ ] Idle performance: record at least one 60s `sample visible-idle 60`; helper count returns to zero and Ryofiles has no sustained busy loop.
- [ ] Memory stability: compare repeated idle samples before/after stress; no unbounded PSS growth.
- [ ] No regression in FileChooser: real GTK/Qt/Chromium/Firefox/Electron/Flatpak checks required by the release checklist still behave correctly.

## Release decision

- [ ] PASS — all hard invariants and required manual gates are green.
- [ ] BLOCK — at least one hard invariant/manual gate failed; attach the failing CSV/snapshot and open a focused issue before any release-candidate version bump.
EOF
    } > "$REPORT_FILE"

    log "report written to $REPORT_FILE"
}

self_test() {
    local original_root="$ROOT"
    local original_results="$RESULTS_DIR"
    local original_fixtures="$FIXTURES_DIR"
    local original_env="$ENV_FILE"
    local original_report="$REPORT_FILE"
    local tmp
    tmp="$(mktemp -d)"

    ROOT="$tmp/evidence"
    RESULTS_DIR="$ROOT/results"
    FIXTURES_DIR="$ROOT/fixtures"
    ENV_FILE="$ROOT/environment.md"
    REPORT_FILE="$ROOT/REPORT.md"

    create_fixtures 8 >/dev/null
    [[ -f "$FIXTURES_DIR/mixed/corrupt.pdf" ]] || die "self-test fixture missing"
    [[ -L "$FIXTURES_DIR/mixed/link-to-regular.txt" ]] || die "self-test symlink missing"
    [[ "$(find "$FIXTURES_DIR/large-directory" -maxdepth 1 -type f | wc -l)" -eq 8 ]] || die "self-test large-directory count mismatch"

    mkdir -p "$RESULTS_DIR"
    printf 'PASS\tself-test\tfixture/report plumbing\n' > "$RESULTS_DIR/gates.tsv"
    write_report >/dev/null
    grep -q 'fixture/report plumbing' "$REPORT_FILE" || die "self-test report missing gate"

    rm -rf "$tmp"
    ROOT="$original_root"
    RESULTS_DIR="$original_results"
    FIXTURES_DIR="$original_fixtures"
    ENV_FILE="$original_env"
    REPORT_FILE="$original_report"
    log "self-test passed"
}

main() {
    local command="${1:-help}"
    shift || true
    case "$command" in
        start)
            ensure_dirs
            : > "$RESULTS_DIR/gates.tsv"
            capture_environment
            create_fixtures "${1:-10000}"
            write_report
            ;;
        fixtures)
            create_fixtures "${1:-10000}"
            ;;
        snapshot)
            snapshot "${1:-snapshot}"
            ;;
        sample)
            sample_processes "${1:-sample}" "${2:-60}" "${3:-1}"
            ;;
        idle-expiry)
            idle_expiry "${1:-15}"
            ;;
        report)
            write_report
            ;;
        self-test)
            self_test
            ;;
        help|-h|--help)
            usage
            ;;
        *)
            usage >&2
            die "unknown command: $command"
            ;;
    esac
}

main "$@"
