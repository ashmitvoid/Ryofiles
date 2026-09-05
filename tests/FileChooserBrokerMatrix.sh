#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s /path/to/ryofiles\n' "$0" >&2
  exit 2
fi

portal_bin=$(realpath "$1")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ -z "${RYOFILES_BROKER_MATRIX_IN_SESSION:-}" ]]; then
  runtime=$(mktemp -d)
  cleanup_runtime() { rm -rf "$runtime"; }
  trap cleanup_runtime EXIT
  chmod 700 "$runtime"
  RYOFILES_BROKER_MATRIX_IN_SESSION=1 \
  XDG_RUNTIME_DIR="$runtime" \
    dbus-run-session -- bash "$0" "$portal_bin"
  exit $?
fi

frontend=/usr/lib/xdg-desktop-portal
if [[ ! -x "$frontend" ]]; then
  printf 'xdg-desktop-portal frontend not found at %s\n' "$frontend" >&2
  exit 1
fi

service_backend='org.freedesktop.impl.portal.desktop.ryofiles'
service_frontend='org.freedesktop.portal.Desktop'
tmp=$(mktemp -d)
backend_pid=''
frontend_pid=''

cleanup() {
  if [[ -n "$frontend_pid" ]] && kill -0 "$frontend_pid" 2>/dev/null; then
    kill "$frontend_pid" 2>/dev/null || true
    wait "$frontend_pid" 2>/dev/null || true
  fi
  if [[ -n "$backend_pid" ]] && kill -0 "$backend_pid" 2>/dev/null; then
    kill "$backend_pid" 2>/dev/null || true
    wait "$backend_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -p \
  "$tmp/config/xdg-desktop-portal" \
  "$tmp/data/xdg-desktop-portal/portals" \
  "$tmp/folder" \
  "$tmp/save-parent" \
  "$tmp/save-files"

cat >"$tmp/config/xdg-desktop-portal/portals.conf" <<'EOF'
[preferred]
default=none
org.freedesktop.impl.portal.FileChooser=ryofiles
EOF

cat >"$tmp/data/xdg-desktop-portal/portals/ryofiles.portal" <<'EOF'
[portal]
DBusName=org.freedesktop.impl.portal.desktop.ryofiles
Interfaces=org.freedesktop.impl.portal.FileChooser;
EOF

export XDG_CONFIG_HOME="$tmp/config"
export XDG_CURRENT_DESKTOP=ryofiles-matrix
export XDG_DATA_HOME="$tmp/data"
export XDG_DATA_DIRS="$tmp/data:/usr/share"
unset XDG_DESKTOP_PORTAL_DIR || true

open_file="$tmp/open.txt"
multi_a="$tmp/multi-a.txt"
multi_b="$tmp/multi-b.txt"
printf 'open\n' >"$open_file"
printf 'multi a\n' >"$multi_a"
printf 'multi b\n' >"$multi_b"

open_uri="file://$open_file"
multi_a_uri="file://$multi_a"
multi_b_uri="file://$multi_b"
folder_uri="file://$tmp/folder"
save_uri="file://$tmp/save-parent/saved.txt"
savefiles_folder_uri="file://$tmp/save-files"

# Occupy alpha.txt so SaveFiles must exercise deterministic collision avoidance.
printf 'occupied\n' >"$tmp/save-files/alpha.txt"
savefiles_a_uri="file://$tmp/save-files/alpha%20%281%29.txt"
savefiles_b_uri="file://$tmp/save-files/beta.txt"

fake_picker="$tmp/fake-picker"
cat >"$fake_picker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

context=$(cat)
mode=''
multiple=no
title=''
for ((i = 1; i <= $#; ++i)); do
  arg=${!i}
  if [[ "$arg" == '--picker' && $i -lt $# ]]; then
    next=$((i + 1))
    mode=${!next}
  fi
  if [[ "$arg" == '--multiple' ]]; then
    multiple=yes
  fi
  if [[ "$arg" == --picker-title=* ]]; then
    title=${arg#--picker-title=}
  fi
done

if [[ "$title" == 'Cancel broker request' ]]; then
  exit 1
fi

case "$mode" in
  open)
    if grep -Fq '"name":"Images"' <<<"$context"; then
      grep -Fq '"initial_filter":1' <<<"$context"
      grep -Fq '"id":"readonly"' <<<"$context"
      grep -Fq '"id":"mode"' <<<"$context"
      printf '{"version":1,"uris":["%s"],"filter":0,"choices":{"readonly":"true","mode":"b"}}\n' \
        "${RYOFILES_MATRIX_OPEN_URI:?}"
    elif [[ "$multiple" == yes ]]; then
      printf '{"version":1,"uris":["%s","%s"],"filter":-1,"choices":{}}\n' \
        "${RYOFILES_MATRIX_MULTI_A_URI:?}" \
        "${RYOFILES_MATRIX_MULTI_B_URI:?}"
    else
      printf '{"version":1,"uris":["%s"],"filter":-1,"choices":{}}\n' \
        "${RYOFILES_MATRIX_OPEN_URI:?}"
    fi
    ;;
  save)
    printf '{"version":1,"uris":["%s"],"filter":-1,"choices":{}}\n' \
      "${RYOFILES_MATRIX_SAVE_URI:?}"
    ;;
  folder)
    if grep -Fq '"id":"savefiles_marker"' <<<"$context"; then
      printf '{"version":1,"uris":["%s"],"filter":-1,"choices":{"savefiles_marker":"true"}}\n' \
        "${RYOFILES_MATRIX_SAVEFILES_FOLDER_URI:?}"
    else
      printf '{"version":1,"uris":["%s"],"filter":-1,"choices":{}}\n' \
        "${RYOFILES_MATRIX_FOLDER_URI:?}"
    fi
    ;;
  *)
    printf 'unexpected picker mode: %s\n' "$mode" >&2
    exit 2
    ;;
esac
EOF
chmod +x "$fake_picker"

name_has_owner() {
  local name=$1
  gdbus call \
    --session \
    --dest org.freedesktop.DBus \
    --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner \
    "$name" 2>/dev/null | grep -q 'true'
}

wait_for_owner() {
  local name=$1
  local i
  for i in $(seq 1 160); do
    if name_has_owner "$name"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

RYOFILES_PICKER_EXECUTABLE="$fake_picker" \
RYOFILES_MATRIX_OPEN_URI="$open_uri" \
RYOFILES_MATRIX_MULTI_A_URI="$multi_a_uri" \
RYOFILES_MATRIX_MULTI_B_URI="$multi_b_uri" \
RYOFILES_MATRIX_FOLDER_URI="$folder_uri" \
RYOFILES_MATRIX_SAVE_URI="$save_uri" \
RYOFILES_MATRIX_SAVEFILES_FOLDER_URI="$savefiles_folder_uri" \
QT_QPA_PLATFORM=offscreen \
  "$portal_bin" --filechooser-portal \
    >"$tmp/backend.out" 2>"$tmp/backend.err" &
backend_pid=$!

if ! wait_for_owner "$service_backend"; then
  printf 'Ryofiles backend did not acquire its session bus name\n' >&2
  cat "$tmp/backend.err" >&2 || true
  exit 1
fi

"$frontend" -v >"$tmp/frontend.out" 2>"$tmp/frontend.err" &
frontend_pid=$!

if ! wait_for_owner "$service_frontend"; then
  printf 'xdg-desktop-portal did not acquire org.freedesktop.portal.Desktop\n' >&2
  cat "$tmp/frontend.out" >&2 || true
  cat "$tmp/frontend.err" >&2 || true
  exit 1
fi

if ! timeout 20s python "$script_dir/FileChooserBrokerMatrixClient.py" \
    "$open_uri" \
    "$multi_a_uri" \
    "$multi_b_uri" \
    "$folder_uri" \
    "$save_uri" \
    "$savefiles_folder_uri" \
    "$savefiles_a_uri" \
    "$savefiles_b_uri"; then
  printf '%s\n' '--- xdg-desktop-portal stdout ---' >&2
  cat "$tmp/frontend.out" >&2 || true
  printf '%s\n' '--- xdg-desktop-portal stderr ---' >&2
  cat "$tmp/frontend.err" >&2 || true
  printf '%s\n' '--- Ryofiles backend stderr ---' >&2
  cat "$tmp/backend.err" >&2 || true
  exit 1
fi

name_has_owner "$service_frontend"
name_has_owner "$service_backend"
printf 'FileChooser public broker request matrix passed\n'
