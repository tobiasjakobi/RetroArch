. qb/config.comp.sh

TEMP_C=.tmp.c
TEMP_EXE=.tmp

ECHOBUF="Checking operating system"
#echo -n "Checking operating system"
case "$(uname)" in
	'Linux') OS='Linux';;
	*'BSD') OS='BSD';;
	*) OS="Linux";;
esac
echo "$ECHOBUF ... $OS"

# Checking for working C compiler
if [ "$USE_LANG_C" = 'yes' ]; then
	ECHOBUF="Checking for suitable working C compiler"
#	echo -n "Checking for suitable working C compiler"
	cat << EOF > "$TEMP_C"
#include <stdio.h>
int main() { puts("Hai world!"); return 0; }
EOF
	if [ -z "$CC" ]; then
		for CC in ${CC:=$(which ${CROSS_COMPILE}gcc ${CROSS_COMPILE}cc ${CROSS_COMPILE}clang)} ''; do
			"$CC" -o "$TEMP_EXE" "$TEMP_C" >/dev/null 2>&1 && break
		done
	fi
	[ "$CC" ] || { echo "$ECHOBUF ... Not found. Exiting."; exit 1;}
	echo "$ECHOBUF ... $CC"
	rm -f "$TEMP_C" "$TEMP_EXE"
fi
