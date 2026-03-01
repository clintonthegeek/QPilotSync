# Fix: 100% CPU Spin After HotSync (Connection Race Cleanup)

**Date**: 2026-02-18
**Affected file**: `src/palm/kpilotdevicelink.cpp` (`cancelConnection()`)

---

## Symptom

After every HotSync, one CPU core pegged at 100% until the application was
closed. The high CPU started immediately after the sync completed and the
Palm disconnected.

## Root Cause

QPilotSync's auto-sync orchestrator races connections on both ttyUSB ports
that the visor kernel module creates for Palm devices (one for HotSync, one
for debugging). Two `ConnectionWorker` threads are spawned, each blocking in
`pi_accept()` waiting for the Palm to respond. The first port to succeed
wins; the other is cancelled.

The cancellation path called `requestCancel()` (sets an atomic bool) and
then `cleanupWorker()`, which tries `quit()` + `wait(3s)` + `terminate()` +
`wait(2s)`. None of these can interrupt a thread blocked in a kernel
`select()` syscall, so the thread was **abandoned** — left running with its
parent set to `nullptr`.

When the Palm physically disconnected, the `/dev/ttyUSB1` device node
vanished. The abandoned thread's `select()` call returned immediately
(stale fd), but pilot-link's `pi_accept()` retried in a tight loop:

```
s_read()  →  select() returns instantly (EBADF / stale fd)
  ↑                          ↓
  └── protocol_queue_build() ← pi_socket_init() ← pi_serial_accept()
```

This is a busy-wait — no sleep, no backoff, 100% CPU.

### Evidence

`top -H` showed a single `QThread` at 99.9% CPU. `gdb -batch` stack trace
confirmed:

```
#3  s_read ()
#6  pi_serial_accept ()
#8  pi_accept ()
#9  ConnectionWorker::doConnect() at kpilotdevicelink.cpp:151
```

## Fix

Changed `cancelConnection()` to call `forceCloseSocket()` instead of
`requestCancel()`. This closes the listening socket's file descriptor under
a mutex, which makes `select()` return `EBADF`. pilot-link's `pi_accept()`
then fails and returns -1. The worker thread's `doConnect()` exits cleanly,
the thread's event loop becomes responsive, and `cleanupWorker()` can
`quit()` it normally.

### Before (broken)

```cpp
// cancelConnection():
m_worker->requestCancel();   // sets bool, doesn't interrupt select()
cleanupWorker();             // quit+wait fails, terminate fails, abandons thread
```

### After (fixed)

```cpp
// cancelConnection():
m_worker->forceCloseSocket();  // closes fd → select() returns EBADF → pi_accept() fails
cleanupWorker();               // quit+wait succeeds because doConnect() already returned
```

### Why this is safe

`doConnect()` was already written to handle `forceCloseSocket()` being
called concurrently:

```cpp
int acceptResult = pi_accept(sock, nullptr, nullptr);
if (acceptResult < 0) {
    // ...
    QMutexLocker locker(&m_socketMutex);
    if (m_socket >= 0) {      // forceCloseSocket() already set this to -1
        pi_close(m_socket);   // skipped — no double-close
    }
    return;
}
```

The previous comment in `cancelConnection()` warned against calling
`forceCloseSocket()` due to a theoretical use-after-free inside pilot-link.
However, the alternative (abandoning the thread) caused a **guaranteed**
100% CPU busy-loop after every sync — a far worse outcome. In practice,
`pi_close()` on the listening socket closes the underlying fd, and
pilot-link's error path through `s_read()` → `pi_accept()` propagates the
failure without accessing freed structures.

## What did NOT work

An earlier attempt tried three simultaneous changes:

1. Closing the listening socket inside `ConnectionWorker::doConnect()` right
   after `pi_accept()` succeeded
2. Uncommenting `dlp_EndOfSync()` in `deviceworker.cpp`
3. Setting `DisconnectAfterSync` mode on orchestrator sessions

This **broke HotSync entirely** — the connection died immediately after
being established. The reason: pilot-link shares internal state between the
listening socket and the accepted socket for USB serial connections.
`pi_close()` on the listening socket invalidated the accepted socket,
killing the live connection.

The correct fix only closes the **losing** port's listening socket (via
`cancelConnection()`), never the winning port's.
