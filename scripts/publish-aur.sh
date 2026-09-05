#!/usr/bin/env bash

set -euo pipefail

readonly AUR_HOST='aur.archlinux.org'
readonly AUR_HOST_ED25519_FINGERPRINT='SHA256:RFzBCUItH9LZS0cKB5UE6ceAYhBD5C8GeOBip8Z11+4'

dry_run=false
pkgrel=1
repository="${GITHUB_REPOSITORY:-krzemin/mpxcast}"
version=''
prepare_dir=''
publish_dir=''

usage() {
    cat <<'EOF'
Usage: scripts/publish-aur.sh --version VERSION [options]

Render, validate, and publish the mpxcast AUR package.

Options:
  --version VERSION  Release version, with or without the leading "v".
  --pkgrel N         Arch package release number (default: 1).
  --repository OWNER/REPO
                    GitHub repository containing the release source archive.
  --prepare DIR      Build and export review artifacts without publishing.
  --publish DIR      Publish previously prepared files without rebuilding.
  --dry-run          Build and stage the AUR update, but do not commit or push.
  -h, --help         Show this help text.

For a non-dry run, AUR_SSH_PRIVATE_KEY must contain the dedicated AUR deploy
key, and AUR_GIT_AUTHOR_NAME and AUR_GIT_AUTHOR_EMAIL must identify the commit
author. The script verifies the AUR SSH host-key fingerprint before using it.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
    --version)
        (($# >= 2)) || die '--version requires a value'
        version="$2"
        shift 2
        ;;
    --pkgrel)
        (($# >= 2)) || die '--pkgrel requires a value'
        pkgrel="$2"
        shift 2
        ;;
    --repository)
        (($# >= 2)) || die '--repository requires a value'
        repository="$2"
        shift 2
        ;;
    --prepare|--publish)
        (($# >= 2)) || die "$1 requires a directory"
        if [[ "$1" == --prepare ]]; then
            prepare_dir="$2"
        else
            publish_dir="$2"
        fi
        shift 2
        ;;
    --dry-run)
        dry_run=true
        shift
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "unknown option: $1"
        ;;
    esac
done

[[ -z "$prepare_dir" || -z "$publish_dir" ]] || die '--prepare and --publish are mutually exclusive'
[[ -z "$publish_dir" || "$dry_run" == false ]] || die '--publish cannot be a dry run'
if [[ -n "$prepare_dir" ]]; then
    dry_run=true
    mkdir -p "$prepare_dir"
    prepare_dir=$(CDPATH='' cd -- "$prepare_dir" && pwd)
    [[ -z $(ls -A "$prepare_dir") ]] || die 'prepare directory must be empty'
fi
if [[ -n "$publish_dir" ]]; then
    publish_dir=$(CDPATH='' cd -- "$publish_dir" && pwd)
fi

version="${version#v}"
[[ "$version" =~ ^[0-9][0-9A-Za-z._+]*$ ]] || die "invalid version: $version"
[[ "$pkgrel" =~ ^[1-9][0-9]*$ ]] || die "invalid pkgrel: $pkgrel"
[[ "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || die "invalid repository: $repository"
[[ $(id -u) -ne 0 ]] || die 'run this script as an unprivileged build user'

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
template_dir="$repo_root/aur"

[[ -f "$template_dir/PKGBUILD.in" ]] || die 'missing aur/PKGBUILD.in'
git -C "$repo_root" diff --quiet -- aur scripts/publish-aur.sh ||
    die 'packaging inputs must be committed before publishing'
packaging_revision=$(git -C "$repo_root" rev-parse HEAD)

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/mpxcast-aur.XXXXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

render_package() {
    local source_dir=$1
    local output_dir=$2
    local render_version=$3
    local render_pkgrel=$4
    local render_sha256=$5
    local render_revision=$6
    local file

    mkdir -p "$output_dir"
    sed \
        -e "s|@PKGVER@|$render_version|g" \
        -e "s|@PKGREL@|$render_pkgrel|g" \
        -e "s|@SOURCE_SHA256@|$render_sha256|g" \
        -e "s|@GITHUB_REPOSITORY@|$repository|g" \
        -e "s|@PACKAGING_REVISION@|$render_revision|g" \
        "$source_dir/PKGBUILD.in" > "$output_dir/PKGBUILD"

    for file in mpxcast.service mpxcast.conf mpxcast.sysusers LICENSE .gitignore; do
        cp "$source_dir/$file" "$output_dir/$file"
    done
}

generate_srcinfo() {
    local package_dir=$1
    (
        cd "$package_dir"
        makepkg --printsrcinfo > .SRCINFO
    )
}

if [[ -z "$publish_dir" ]]; then
    source_archive="$work_dir/mpxcast-$version.tar.gz"
    source_url="https://github.com/$repository/archive/refs/tags/v$version.tar.gz"

    echo "Downloading $source_url"
    curl --fail --location --silent --show-error --output "$source_archive" "$source_url"
    source_sha256=$(sha256sum "$source_archive" | awk '{print $1}')

    mkdir "$work_dir/source"
    tar -xzf "$source_archive" -C "$work_dir/source"
    source_root=$(find "$work_dir/source" -mindepth 1 -maxdepth 1 -type d -print -quit)
    [[ -n "$source_root" ]] || die 'release archive did not contain a source directory'
    source_version=$(sed -nE 's/^project\(mpxcast VERSION ([^ )]+).*/\1/p' "$source_root/CMakeLists.txt")
    [[ "$source_version" == "$version" ]] ||
        die "release archive version $source_version does not match requested version $version"

    rendered_dir="$work_dir/rendered"
    render_package "$template_dir" "$rendered_dir" "$version" "$pkgrel" "$source_sha256" "$packaging_revision"

    echo "Building mpxcast $version-$pkgrel"
    (
        cd "$rendered_dir"
        makepkg --verifysource
        makepkg --syncdeps --cleanbuild --noconfirm
    )
    generate_srcinfo "$rendered_dir"

else
    (cd "$publish_dir" && sha256sum --check SHA256SUMS)
    [[ $(cat "$publish_dir/version") == "$version-$pkgrel" ]] || die 'prepared version does not match request'
    [[ $(cat "$publish_dir/repository") == "$repository" ]] || die 'prepared repository does not match request'
    [[ $(cat "$publish_dir/packaging-revision") == "$packaging_revision" ]] || die 'prepared packaging revision does not match checkout'
    rendered_dir="$publish_dir/packaging"
fi

clone_aur_repository() {
    local checkout_dir=$1
    local clone_url

    if "$dry_run"; then
        clone_url="https://$AUR_HOST/mpxcast.git"
        git -c init.defaultBranch=master clone --quiet "$clone_url" "$checkout_dir"
        return
    fi

    [[ -n "${AUR_SSH_PRIVATE_KEY:-}" ]] || die 'AUR_SSH_PRIVATE_KEY is required for publishing'
    [[ -n "${AUR_GIT_AUTHOR_NAME:-}" ]] || die 'AUR_GIT_AUTHOR_NAME is required for publishing'
    [[ -n "${AUR_GIT_AUTHOR_EMAIL:-}" ]] || die 'AUR_GIT_AUTHOR_EMAIL is required for publishing'

    local ssh_dir="$work_dir/ssh"
    local ssh_key="$ssh_dir/id_ed25519"
    local known_hosts="$ssh_dir/known_hosts"
    local fingerprint

    mkdir -m 700 "$ssh_dir"
    printf '%s\n' "$AUR_SSH_PRIVATE_KEY" > "$ssh_key"
    chmod 600 "$ssh_key"
    ssh-keyscan -t ed25519 "$AUR_HOST" > "$known_hosts" 2>/dev/null
    fingerprint=$(ssh-keygen -lf "$known_hosts" -E sha256 | awk '{print $2}')
    [[ "$fingerprint" == "$AUR_HOST_ED25519_FINGERPRINT" ]] ||
        die "unexpected $AUR_HOST ED25519 host-key fingerprint: $fingerprint"

    export GIT_SSH_COMMAND="ssh -i $ssh_key -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$known_hosts"
    clone_url="ssh://aur@$AUR_HOST/mpxcast.git"
    git -c init.defaultBranch=master clone --quiet "$clone_url" "$checkout_dir"
}

validate_existing_aur_package() {
    local aur_dir=$1
    local remote_revision
    local remote_version
    local remote_pkgrel
    local remote_sha256
    local historical_dir="$work_dir/historical"
    local expected_dir="$work_dir/expected"

    [[ -f "$aur_dir/PKGBUILD" ]] || return 0

    remote_revision=$(sed -nE 's/^# Managed from .* packaging revision: ([0-9a-f]{40})$/\1/p' \
        "$aur_dir/PKGBUILD")
    [[ -n "$remote_revision" ]] || die 'existing AUR PKGBUILD has no managed packaging revision'
    git -C "$repo_root" merge-base --is-ancestor "$remote_revision" HEAD ||
        die 'existing AUR packaging revision is not an ancestor of this checkout'

    remote_version=$(sed -nE 's/^pkgver=([0-9A-Za-z._+]+)$/\1/p' "$aur_dir/PKGBUILD")
    remote_pkgrel=$(sed -nE 's/^pkgrel=([1-9][0-9]*)$/\1/p' "$aur_dir/PKGBUILD")
    remote_sha256=$(sed -nE "s/^sha256sums=\('([0-9a-f]{64})'.*/\1/p" "$aur_dir/PKGBUILD")
    [[ -n "$remote_version" && -n "$remote_pkgrel" && -n "$remote_sha256" ]] ||
        die 'could not read version, pkgrel, or source checksum from existing AUR PKGBUILD'

    mkdir "$historical_dir"
    git -C "$repo_root" archive "$remote_revision" aur | tar -x -C "$historical_dir"
    [[ -f "$historical_dir/aur/PKGBUILD.in" ]] ||
        die 'existing AUR packaging revision does not contain aur/PKGBUILD.in'

    render_package "$historical_dir/aur" "$expected_dir" "$remote_version" "$remote_pkgrel" \
        "$remote_sha256" "$remote_revision"
    generate_srcinfo "$expected_dir"

    if ! diff -ru --exclude=.git "$expected_dir" "$aur_dir"; then
        die 'existing AUR package differs from its recorded upstream packaging revision'
    fi
}

aur_checkout="$work_dir/aur"
clone_aur_repository "$aur_checkout"
aur_baseline=$(git -C "$aur_checkout" rev-parse --verify HEAD 2>/dev/null || printf 'empty')
if [[ -n "$publish_dir" ]]; then
    [[ $(cat "$publish_dir/aur-baseline") == "$aur_baseline" ]] ||
        die 'AUR changed since preparation; prepare and review a new run'
else
    validate_existing_aur_package "$aur_checkout"
fi

find "$aur_checkout" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
cp "$rendered_dir/PKGBUILD" "$rendered_dir/.SRCINFO" "$rendered_dir/mpxcast.service" \
    "$rendered_dir/mpxcast.conf" "$rendered_dir/mpxcast.sysusers" "$rendered_dir/LICENSE" \
    "$rendered_dir/.gitignore" "$aur_checkout/"

git -C "$aur_checkout" add --all
if [[ -n "$prepare_dir" ]]; then
    mkdir "$prepare_dir/packaging" "$prepare_dir/packages"
    for file in PKGBUILD .SRCINFO mpxcast.service mpxcast.conf mpxcast.sysusers LICENSE .gitignore; do
        cp "$rendered_dir/$file" "$prepare_dir/packaging/"
    done
    cp "$rendered_dir/"*.pkg.tar.* "$prepare_dir/packages/"
    cp "$source_archive" "$prepare_dir/"
    printf '%s\n' "$version-$pkgrel" > "$prepare_dir/version"
    printf '%s\n' "$repository" > "$prepare_dir/repository"
    printf '%s\n' "$packaging_revision" > "$prepare_dir/packaging-revision"
    printf '%s\n' "$aur_baseline" > "$prepare_dir/aur-baseline"
    git -C "$repo_root" rev-parse "v$version^{commit}" > "$prepare_dir/source-revision"
    git -C "$aur_checkout" diff --cached > "$prepare_dir/aur.diff"
    for package in "$prepare_dir/packages/"*.pkg.tar.*; do
        pacman -Qip "$package"
        pacman -Qlp "$package"
    done > "$prepare_dir/package-info.txt"
    (
        cd "$prepare_dir"
        # Markdown backticks are literal.
        # shellcheck disable=SC2016
        {
            printf '# AUR package review\n\n'
            printf -- '- Package: `mpxcast %s-%s` (built on x86_64)\n' "$version" "$pkgrel"
            printf -- '- Source: [%s](%s)\n' "$source_url" "$source_url"
            printf -- '- Source commit: `%s`\n' "$(cat source-revision)"
            printf -- '- Source SHA256: `%s`\n' "$source_sha256"
            printf -- '- Packaging commit: `%s`\n' "$packaging_revision"
            printf -- '- AUR baseline: `%s`\n\n' "$aur_baseline"
            printf 'Download the **aur-review** artifact from this workflow run to inspect the built package, source archive, packaging files, package file list, diff, and SHA256SUMS. Only packaging files are pushed to AUR.\n\n'
            printf '## Built package metadata and files\n\n```text\n'
            cat package-info.txt
            printf '```\n\n## Proposed AUR diff\n\n```diff\n'
            cat aur.diff
            printf '```\n'
        } > review.md
        find . -type f -print0 | sort -z | xargs -0 sha256sum > "$work_dir/SHA256SUMS"
        cp "$work_dir/SHA256SUMS" .
    )
    exit 0
fi

if git -C "$aur_checkout" diff --cached --quiet; then
    echo "AUR package is already at $version-$pkgrel."
    exit 0
fi

if "$dry_run"; then
    echo 'Dry run: the following AUR change was validated and staged:'
    git -C "$aur_checkout" diff --cached --stat
    git -C "$aur_checkout" diff --cached
    exit 0
fi

git -C "$aur_checkout" config user.name "$AUR_GIT_AUTHOR_NAME"
git -C "$aur_checkout" config user.email "$AUR_GIT_AUTHOR_EMAIL"
git -C "$aur_checkout" commit -m "Update to $version-$pkgrel"
git -C "$aur_checkout" push origin HEAD:master
