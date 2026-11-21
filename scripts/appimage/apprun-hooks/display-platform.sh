# Riverhorse runs natively on Wayland when the session provides it. Set RIVERHORSE_X11=1 to run through X11 instead.
if [[ -n "$RIVERHORSE_X11" ]]; then
	export QT_QPA_PLATFORM=xcb
fi
