# Source this to use Open Watcom v2 (installed in ~/.local/opt/openwatcom).
export WATCOM="${WATCOM:-$HOME/.local/opt/openwatcom}"
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
