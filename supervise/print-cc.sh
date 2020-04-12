cc="`head -1 conf-cc`"
systype="`cat systype`"

cat warn-auto.sh
echo exec "$cc" '-c -Wall ${1+"$@"}'
#echo exec "$cc" '-c  -include /usr/include/errno.h ${1+"$@"}'
