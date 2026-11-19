#!/bin/bash
# Atajo para vita-agent-use (cliente emparejado en WSL). Nunca lanzar dos a la vez (el agente admite una sola sesion).
#   tools/va.sh <subcomando...>     p. ej.  tools/va.sh system snapshot
# La ruta WSL del script del agente va en tools/local.env (no se sube a git):
#   VITA_AGENT_SCRIPT=/mnt/c/.../wsl-agent.sh
cd "$(dirname "$0")/.."
[ -f tools/local.env ] && . tools/local.env
: "${VITA_AGENT_SCRIPT:?Define VITA_AGENT_SCRIPT en tools/local.env (ruta WSL de wsl-agent.sh)}"
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash "$VITA_AGENT_SCRIPT" "$@" 2>&1 | tr -d '\0' | grep -v 'systemd user session'
