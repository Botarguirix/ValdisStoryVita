#!/bin/bash
# Sube build/vita/vsvita.vpk (o $VPK) a la Vita, lo instala y lanza VALDIS001 (vita-agent-use).
#   tools/deploy.sh [--no-launch]        VPK=build/vita_noaot/vsvita.vpk tools/deploy.sh
cd "$(dirname "$0")/.."
VA=tools/va.sh
p=$(pwd -W); d=${p:0:1}; REPO_WSL="/mnt/${d,,}${p:2}"   # ruta del repo vista desde WSL (C:/x -> /mnt/c/x)
$VA app close VALDIS001 >/dev/null 2>&1; sleep 4
name="vsvita_$(date +%H%M%S).vpk"
for try in 1 2 3 4; do
  out=$($VA fs upload --yes "$REPO_WSL/${VPK:-build/vita/vsvita.vpk}" ux0:downloads/$name)
  echo "$out" | grep -q '"complete":true' && break
  sleep 3
done
echo "$out" | grep -q '"complete":true' || { echo "subida fallida"; echo "$out" | tail -c 400; exit 1; }
echo "subido"
for try in 1 2 3; do
  st=$($VA app install --yes --timeout 240 ux0:downloads/$name | grep -o '"state":"[a-z]*"')
  echo "instalacion: $st"
  [ "$st" = '"state":"installed"' ] && break
  $VA app close VALDIS001 >/dev/null 2>&1; sleep 5
done
# el VPK subido va a la papelera del agente (ruta unica: nunca se reutiliza, ver docs/TESTING.md)
$VA fs trash --yes ux0:downloads/$name >/dev/null 2>&1
[ "$st" = '"state":"installed"' ] || exit 1
[ "$1" = "--no-launch" ] || $VA app launch VALDIS001 | grep -o '"foreground":{[^}]*}'
