#!/bin/bash
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-prf-tests.sh [qmake]
#
# The qmake features in features/, as components use them: a small project
# per case, configured with this tree's features first in the search path,
# installed with INSTALL_ROOT, and the installed tree compared with what it
# should be. qmake is $1, $QMAKE, or qmake6 on PATH (with its libraries
# found, e.g. LD_LIBRARY_PATH=<native sysroot>/usr/lib).

set -u

SRC=$(cd "$(dirname "$0")/../.." && pwd)
QMAKE=${1:-${QMAKE:-qmake6}}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/prf-tests.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
export QMAKEFEATURES="$SRC/features"
# What the webOS build environment would otherwise hand webos-variables.prf.
unset base_prefix prefix datadir sysconfdir sysbus_datadir webos_sysbus_servicedir \
      webos_sysbus_rolesdir webos_sysbus_apipermissionsdir webos_sysbus_permissionsdir \
      webos_sysbus_groupsdir webos_upstartconfdir

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1"; [ $# -gt 1 ] && printf '%s\n' "$2" | sed 's/^/     /'; }

# project <dir> <pro contents>: writes <dir>/p.pro, runs qmake and make
# install into <dir>/root. Output in <dir>/log; returns qmake's status, then
# make's.
project() {
    local d=$1
    mkdir -p "$d/build" "$d/root"
    printf '%s\n' "$2" > "$d/p.pro"
    ( cd "$d/build" && "$QMAKE" ../p.pro ) > "$d/log" 2>&1 || return 1
    make -C "$d/build" install INSTALL_ROOT="$d/root" >> "$d/log" 2>&1 || return 2
}

# installed <dir>: the files under <dir>/root, relative, sorted.
installed() { ( cd "$1/root" && find . -type f | sed 's|^\./||' | sort ); }

expect_tree() {
    local name=$1 d=$2 want=$3 got
    got=$(installed "$d")
    if [ "$got" == "$want" ]; then ok "$name"; else
        bad "$name" "$(diff <(echo "$want") <(echo "$got"))"; fi
}

LS2=usr/share/luna-service2

# --- webos-service: ACG layout, a name that contains "service" ------------
d=$WORK/acg
mkdir -p "$d/sysbus"
n=com.webos.service.ime
# Quotes escaped, as the components' own templates have them: qmake's
# substitution would otherwise drop them.
printf '{\\"exeName\\":\\"$$[QT_INSTALL_BINS]/ime\\"}\n' > "$d/sysbus/$n.role.json.in"
printf '[D-BUS Service]\nName=%s\nExec=$$[QT_INSTALL_BINS]/ime\n' $n > "$d/sysbus/$n.service.in"
for t in api perm groups; do echo '{}' > "$d/sysbus/$n.$t.json"; done
if project "$d" "TEMPLATE = aux
WEBOS_SYSBUS_DIR = sysbus
WEBOS_INSTALL_ROOT = /
CONFIG += webos-service"; then
    expect_tree "webos-service: ACG files each installed once, in their own directory" "$d" \
"$LS2/api-permissions.d/$n.api.json
$LS2/client-permissions.d/$n.perm.json
$LS2/groups.d/$n.groups.json
$LS2/roles.d/$n.role.json
$LS2/services.d/$n.service"
    bins=$("$QMAKE" -query QT_INSTALL_BINS)
    if grep -qF "\"$bins/ime\"" "$d/root/$LS2/roles.d/$n.role.json" &&
       grep -qF "Exec=$bins/ime" "$d/root/$LS2/services.d/$n.service"; then
        ok "webos-service: .in files substituted"
    else
        bad "webos-service: .in files substituted" "$(cat "$d/root/$LS2/roles.d/$n.role.json" "$d/root/$LS2/services.d/$n.service")"
    fi
else
    bad "webos-service: ACG project configures and installs" "$(tail -20 "$d/log")"
fi

# --- webos-service: names without "service" -------------------------------
d=$WORK/acg2
mkdir -p "$d/sysbus"
for n in com.palm.smtp browser-shell; do
    echo '{}' > "$d/sysbus/$n.role.json.in"
    echo '[D-BUS Service]' > "$d/sysbus/$n.service.in"
done
if project "$d" "TEMPLATE = aux
WEBOS_SYSBUS_DIR = sysbus
WEBOS_INSTALL_ROOT = /
CONFIG += webos-service"; then
    expect_tree "webos-service: names without \"service\" in them" "$d" \
"$LS2/roles.d/browser-shell.role.json
$LS2/roles.d/com.palm.smtp.role.json
$LS2/services.d/browser-shell.service
$LS2/services.d/com.palm.smtp.service"
else
    bad "webos-service: names without \"service\" configure" "$(tail -20 "$d/log")"
fi

# --- webos-service: the type words inside a name --------------------------
d=$WORK/acg3
mkdir -p "$d/sysbus"
for n in com.webos.service.permissionmanager com.example.apigateway com.example.groupchat; do
    echo '{}' > "$d/sysbus/$n.role.json.in"
    for t in api perm groups; do echo '{}' > "$d/sysbus/$n.$t.json"; done
done
echo '{}' > "$d/sysbus/com.example.single.group.json"
if project "$d" "TEMPLATE = aux
WEBOS_SYSBUS_DIR = sysbus
WEBOS_INSTALL_ROOT = /
CONFIG += webos-service"; then
    want=$(for n in com.webos.service.permissionmanager com.example.apigateway com.example.groupchat; do
        echo "$LS2/api-permissions.d/$n.api.json"
        echo "$LS2/client-permissions.d/$n.perm.json"
        echo "$LS2/groups.d/$n.groups.json"
        echo "$LS2/roles.d/$n.role.json"
    done; echo "$LS2/groups.d/com.example.single.group.json")
    expect_tree "webos-service: \"api\", \"perm\", \"group\" inside a name match once" "$d" "$(echo "$want" | sort)"
else
    bad "webos-service: type words in names configure" "$(tail -20 "$d/log")"
fi

# --- webos-service: the legacy pub/prv layout -----------------------------
d=$WORK/legacy
mkdir -p "$d/sysbus/roles-pub" "$d/sysbus/services-prv"
echo '{}' > "$d/sysbus/roles-pub/com.example.app.in"
echo '[D-BUS Service]' > "$d/sysbus/services-prv/com.example.app.service.in"
if project "$d" "TEMPLATE = aux
WEBOS_SYSBUS_DIR = sysbus
WEBOS_INSTALL_ROOT = /
CONFIG += webos-service"; then
    expect_tree "webos-service: legacy layout still installed" "$d" \
"usr/share/dbus-1/system-services/com.example.app.service
usr/share/ls2/roles/pub/com.example.app"
    if grep -q 'Deprecated layout' "$d/log"; then ok "webos-service: legacy layout warned about"
    else bad "webos-service: legacy layout warned about" "$(cat "$d/log")"; fi
else
    bad "webos-service: legacy project configures" "$(tail -20 "$d/log")"
fi

# --- webos-service: no WEBOS_SYSBUS_DIR -----------------------------------
d=$WORK/nodir
if project "$d" "TEMPLATE = aux
CONFIG += webos-service"; then
    bad "webos-service: refuses to configure without WEBOS_SYSBUS_DIR"
elif grep -q 'WEBOS_SYSBUS_DIR is not set' "$d/log"; then
    ok "webos-service: refuses to configure without WEBOS_SYSBUS_DIR"
else
    bad "webos-service: refuses to configure without WEBOS_SYSBUS_DIR" "$(cat "$d/log")"
fi

# --- webos-variables: webosSet --------------------------------------------
d=$WORK/vars
if project "$d" 'TEMPLATE = aux
load(webos-variables)
webosSet(FIRST, a, b)
webosSet(THIRD, , , c)
webosSet(FIFTH, , , , , e)
NOTSET = kept
webosSet(NOTSET, other)
webosSet(EMPTY, , , , , )
defined(EMPTY, var): EMPTY_STATE = defined
else: EMPTY_STATE = undefined
message(RESULT FIRST=$$FIRST THIRD=$$THIRD FIFTH=$$FIFTH NOTSET=$$NOTSET EMPTY=$$EMPTY_STATE)
message(RESULT ROLES=$$WEBOS_INSTALL_SYSBUS_ROLESDIR UPSTART=$$WEBOS_INSTALL_UPSTARTCONFDIR)'; then
    got=$(grep -o 'RESULT .*' "$d/log" | head -1)
    want="RESULT FIRST=a THIRD=c FIFTH=e NOTSET=kept EMPTY=undefined"
    if [ "$got" == "$want" ]; then ok "webos-variables: webosSet takes the first non-empty of five defaults"
    else bad "webos-variables: webosSet takes the first non-empty of five defaults" "want: $want
got:  $got"; fi
    got=$(grep -o 'RESULT ROLES.*' "$d/log")
    want="RESULT ROLES=/usr/share/luna-service2/roles.d UPSTART=/etc/event.d"
    if [ "$got" == "$want" ]; then ok "webos-variables: default directories"
    else bad "webos-variables: default directories" "want: $want
got:  $got"; fi
else
    bad "webos-variables: project configures" "$(tail -20 "$d/log")"
fi

d=$WORK/varsenv
if base_prefix=/opt/r webos_sysbus_rolesdir=/x/roles project "$d" 'TEMPLATE = aux
load(webos-variables)
message(RESULT $$WEBOS_INSTALL_PREFIX $$WEBOS_INSTALL_SYSBUS_ROLESDIR $$WEBOS_INSTALL_SYSBUS_SERVICEDIR)'; then
    got=$(grep -o 'RESULT .*' "$d/log")
    want="RESULT /opt/r/usr /x/roles /opt/r/usr/share/luna-service2/services.d"
    if [ "$got" == "$want" ]; then ok "webos-variables: the environment overrides the defaults"
    else bad "webos-variables: the environment overrides the defaults" "want: $want
got:  $got"; fi
else
    bad "webos-variables: env project configures" "$(tail -20 "$d/log")"
fi

# --- webos: upstart files -------------------------------------------------
d=$WORK/upstart
mkdir -p "$d/files"
printf 'exec $$[QT_INSTALL_BINS]/thing\n' > "$d/files/thing.conf.in"
if project "$d" "TEMPLATE = aux
WEBOS_INSTALL_ROOT = /
WEBOS_UPSTART_FILES_IN = files/thing.conf.in
CONFIG += webos"; then
    expect_tree "webos: upstart file substituted and installed" "$d" "etc/event.d/thing.conf"
    if grep -qF "exec $("$QMAKE" -query QT_INSTALL_BINS)/thing" "$d/root/etc/event.d/thing.conf"; then
        ok "webos: upstart file substituted"
    else
        bad "webos: upstart file substituted" "$(cat "$d/root/etc/event.d/thing.conf")"
    fi
else
    bad "webos: upstart project configures" "$(tail -20 "$d/log")"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
