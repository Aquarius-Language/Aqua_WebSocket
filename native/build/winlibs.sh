#!/bin/sh
# Generates MinGW-style import libraries for the Windows DLLs we link against.
set -e
OUT=${1:-.}
mkdir -p "$OUT"
cat > "$OUT/ws2_32.def" <<'D'
LIBRARY ws2_32.dll
EXPORTS
WSAStartup
socket
bind
listen
accept
connect
send
recv
sendto
recvfrom
closesocket
ioctlsocket
setsockopt
getsockopt
getsockname
WSAPoll
WSAGetLastError
WSAIoctl
getaddrinfo
freeaddrinfo
gethostname
inet_pton
D
cat > "$OUT/kernel32.def" <<'D'
LIBRARY kernel32.dll
EXPORTS
GetProcessHeap
HeapAlloc
HeapReAlloc
HeapFree
QueryPerformanceCounter
QueryPerformanceFrequency
GetSystemTimeAsFileTime
SwitchToThread
Sleep
D
cat > "$OUT/bcrypt.def" <<'D'
LIBRARY bcrypt.dll
EXPORTS
BCryptGenRandom
D
# Test programs additionally load DLLs and write to the console.
{ sed '/^SwitchToThread$/q' "$OUT/kernel32.def"; printf 'Sleep\nLoadLibraryA\nGetProcAddress\nGetStdHandle\nWriteFile\nExitProcess\n'; } > "$OUT/kernel32x.def"
for n in ws2_32 kernel32 kernel32x bcrypt; do llvm-dlltool -m i386:x86-64 -d "$OUT/$n.def" -l "$OUT/$n.lib"; done
