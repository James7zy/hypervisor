#!/usr/bin/env bash
# Point .git/hooks/commit-msg at check-commit-message so the format fails locally.
set -eu

repo=$(git rev-parse --show-toplevel)
checker="$repo/scripts/commit-message/check-commit-message"
hook="$repo/.git/hooks/commit-msg"

printf '#!/usr/bin/env bash\nexec "%s" "$1"\n' "$checker" > "$hook"
chmod +x "$checker" "$hook"
printf 'installed %s\n' "$hook"
