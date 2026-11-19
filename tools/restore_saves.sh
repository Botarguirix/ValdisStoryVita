#!/bin/bash
# Restaura las partidas de la copia $1 en la Vita.
# NUNCA usar "fs trash" sobre una ruta y reutilizarla: la papelera del agente actua tarde, por ruta, y borra el
# archivo nuevo (asi se perdieron las partidas dos veces el 19-11). Tampoco fiarse de "fs download" para comprobar
# (devolvio copias que ya no existian). Metodo: el archivo actual se aparta con "fs move" a un nombre unico, el nuevo
# se sube con nombre unico y se mueve a su sitio; se comprueba con "fs list" (en vivo) tras unos segundos.
cd "$(dirname "$0")/.."
B=$1; V=tools/va.sh; T=$(date +%H%M%S)
p=$(pwd -W); d=${p:0:1}; REPO_WSL="/mnt/${d,,}${p:2}"
files="vs_settings.ini vvva.dat vvvd.dat vvvo.dat vvvr.dat"
cur=$($V fs list ux0:data/valdis/save)
for f in $files; do
  if echo "$cur" | grep -q "\"name\":\"$f\""; then $V fs move ux0:data/valdis/save/$f ux0:data/valdis/save/old${T}_$f >/dev/null; fi
  $V fs upload --yes "$REPO_WSL/$B/$f" ux0:data/valdis/save/u${T}_$f >/dev/null
  $V fs move ux0:data/valdis/save/u${T}_$f ux0:data/valdis/save/$f >/dev/null
done
sleep 15
ok=1; cur=$($V fs list ux0:data/valdis/save)
for f in $files; do
  sz=$(stat -c %s $B/$f)
  echo "$cur" | grep -q "\"name\":\"$f\",\"kind\":\"file\",\"bytes\":\"$sz\"" && echo "$f en su sitio ($sz bytes)" || { echo "$f FALTA o tamano distinto"; ok=0; }
done
[ $ok = 1 ] && echo "PARTIDAS OK (las anteriores quedan como old${T}_*)"
