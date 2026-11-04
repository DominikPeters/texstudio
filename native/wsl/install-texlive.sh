#!/bin/sh
# User-local TeX Live installation, invoked only by the setup wizard's Install
# button. GPL-3.0-only. Never modifies the system PATH or an existing TeX tree.
set -eu
scheme=${1:-full}
case "$scheme" in small|full) ;; *) echo 'Choose small or full.' >&2; exit 2;; esac
for tool in curl perl tar; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Install $tool inside this WSL distribution, then retry." >&2; exit 1; }
done
destination="$HOME/.local/share/texlive/espresso"
if [ -e "$destination" ]; then
    echo "An installation already exists at $destination. Use it or repair it before retrying; it will not be overwritten." >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
printf 'Downloading the official TeX Live installer.\nInstallation directory: %s\n' "$destination"
url=$(curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 \
    --connect-timeout 20 --output "$work/install.tar.gz" --write-out '%{url_effective}' \
    https://mirror.ctan.org/systems/texlive/tlnet/install-tl-unx.tar.gz)
repository=${url%/install-tl-unx.tar.gz}
tar -xzf "$work/install.tar.gz" -C "$work"
installer=
for candidate in "$work"/install-tl-*/install-tl; do [ ! -f "$candidate" ] || installer="$candidate"; done
[ -n "$installer" ] || { echo 'The installer archive did not contain install-tl.' >&2; exit 1; }
cat > "$work/profile" <<PROFILE
selected_scheme scheme-$scheme
TEXDIR $destination
TEXMFHOME $HOME/texmf
TEXMFLOCAL $destination/texmf-local
TEXMFSYSCONFIG $destination/texmf-config
TEXMFSYSVAR $destination/texmf-var
TEXMFCONFIG $HOME/.texlive-espresso/texmf-config
TEXMFVAR $HOME/.texlive-espresso/texmf-var
instopt_adjustpath 0
PROFILE
perl "$installer" --profile "$work/profile" --no-interaction --repository "$repository"
for directory in "$destination"/bin/*; do
    [ ! -x "$directory/kpsewhich" ] || PATH="$directory:$PATH"
done
export PATH
if [ "$scheme" = small ]; then
    tlmgr --repository "$repository" install biber biblatex latexmk collection-latexrecommended collection-fontsrecommended
fi
mkdir "$work/check"
printf '\\documentclass{article}\n\\begin{document}TeXstudio Espresso setup check.\\end{document}\n' > "$work/check/main.tex"
cd "$work/check"
if ! xelatex -interaction=nonstopmode -halt-on-error main.tex; then
    echo 'TeX Live installed, but the verification compilation failed. See the output above.' >&2
    exit 1
fi
printf '\nTeX Live installed and a test document compiled successfully.\n'
