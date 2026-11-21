#!/usr/bin/env bash
set -euo pipefail

output_dir="$(pwd)/dist"

RIVERHORSE_CONTRIBUTORS=""
page=1
while :; do
  logins="$(curl -fsSL -H "Authorization: Bearer ${CONTRIBKIT_GITHUB_CONTRIBUTORS_TOKEN}" \
    "https://api.github.com/repos/${GITHUB_REPOSITORY}/compare/v0.1-7371...HEAD?per_page=100&page=${page}" |
    jq -r '.commits[].author.login // empty')"
  [[ -z "${logins}" ]] && break
  RIVERHORSE_CONTRIBUTORS+=" ${logins//$'\n'/ }"
  page=$((page + 1))
done
export RIVERHORSE_CONTRIBUTORS

pushd contributors >/dev/null
CONTRIBKIT_GITHUB_CONTRIBUTORS_LOGIN="${GITHUB_REPOSITORY%%/*}" \
  CONTRIBKIT_GITHUB_CONTRIBUTORS_REPO="${GITHUB_REPOSITORY#*/}" \
  npx contribkit --outputDir="${output_dir}" -w=800 --name=github --force
popd >/dev/null

if [[ -n "${CONTRIBKIT_GITHUB_TOKEN:-}" ]]; then
  pushd sponsors >/dev/null
  CONTRIBKIT_GITHUB_LOGIN="${GITHUB_REPOSITORY%%/*}" \
    CONTRIBKIT_GITHUB_TYPE=user \
    npx contribkit --outputDir="${output_dir}" -w=800 --name=sponsors --force
  popd >/dev/null
fi
