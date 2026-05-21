# TODO: Improve Plugin Subprocess Output Handling

## Problem

When a conduit plugin runs an external process (e.g., Plucker invoking
`Spider.py`), errors are truncated at 500 characters before being emitted via
`errorOccurred()`. This makes diagnosing failures difficult because Python
tracebacks are typically longer than 500 characters.

Current code in `pluckerconduit.cpp:spiderChannel()`:

```cpp
Q_EMIT errorOccurred(QStringLiteral("Plucker error for %1: %2")
                     .arg(channel.name, output.left(500)));
```

## What We Need

1. **Full stderr/stdout capture** -- emit the complete process output to the log
   system, not just the first 500 chars of a prefix-formatted error string.

2. **Structured log levels** -- separate the summary error (for the UI) from the
   full diagnostic output (for the log pane / log file).

3. **Scrollable log pane** -- the main window log area should let users scroll
   back through full output from plugin subprocesses.

4. **Per-channel log files** -- optionally write each Spider.py invocation's
   full stdout/stderr to a file under the sync folder for post-mortem debugging.

## Affected Conduits

- Plucker (subprocess: `Spider.py`)
- Any future conduit that shells out to external tools
