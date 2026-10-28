#!/bin/bash
# PreToolUse guard for Bash: refuses to change the running Hyprland's config.
# `hyprctl keyword`, `repl` and `eval` against the live session once froze the
# compositor on the tablet, with no keyboard attached to recover. Read-only
# queries, dispatch and reload pass. A command aimed at another instance
# (`hyprctl -i`/`--instance`, or its own HYPRLAND_INSTANCE_SIGNATURE) passes
# too, which is how a nested Hyprland is driven; instance 0 is the live one.

command=$(jq -r '.tool_input.command // empty')
[[ $command == *hyprctl* ]] || exit 0

if [[ $command =~ (^|[^[:alnum:]_-])(-i|--instance)[[:space:]]+[^0[:space:]] || $command == *HYPRLAND_INSTANCE_SIGNATURE=* ]]; then
  exit 0
fi

# hyprctl, any flags and words, then a config-changing request (also inside
# --batch), within one command of a pipeline or list.
if [[ $command =~ hyprctl([[:space:]]+[^[:space:];|&]+)*[[:space:]]+(keyword|repl|eval)([[:space:]]|$) ||
      $command =~ hyprctl.*--batch.*(keyword|repl|eval) ]]; then
  cat >&2 <<'EOF'
Blocked: this changes the live Hyprland config (hyprctl keyword/repl/eval), which
once froze the tablet's compositor. Try the change in a nested Hyprland instead
(see lock/tests/try), or ask the user to run it themselves.
EOF
  exit 2
fi
exit 0
