# PyPlucker Testing Guide for Developers

This document describes how QPilotSync invokes PyPlucker's `Spider.py` so that
PyPlucker developers can reproduce and test the exact command-line combinations
our GUI allows users to configure.

## Integration Architecture

QPilotSync does **not** import any Python modules or touch PyPlucker internals.
The integration is a pure subprocess call:

```
python3  Spider.py  [OPTIONS]
```

The C++ conduit (`pluckerconduit.cpp`) does the following:

1. Finds `python3` on `$PATH`
2. Resolves `Spider.py` via compile-time `PLUCKER_DATA_DIR` macro
3. Sets `PYTHONPATH` to `Spider.py`'s parent's parent directory (so that
   `import PyPlucker` works from within Spider.py)
4. Builds a CLI argument list from the channel configuration
5. Runs `QProcess::start(python3, [spider.py, ...args])`
6. Waits up to 5 minutes for the process to finish
7. Checks exit code -- 0 means success
8. Looks for the output `.pdb` file at `<pluckerdir>/<doc-file>.pdb`

**No Python classes are instantiated from C++. No config files are written for
Spider.py to read. The entire interface is CLI arguments and exit code.**

## Channel Configuration Fields

Our GUI (PluckerChannelDialog) exposes these options across 5 tabs. Each maps
to a Spider.py CLI flag:

### Tab 1: Starting Page

| GUI Field      | CLI Flag               | Type   | Default | Notes                    |
|----------------|------------------------|--------|---------|--------------------------|
| URL            | `--home-url=<url>`     | string | —       | Required                 |
| Document Name  | `--doc-name=<name>`    | string | —       | Display name in Plucker  |
| —              | `--doc-file=<name>`    | string | —       | Sanitized from doc-name  |
| Category       | `--category=<name>`    | string | (none)  | Only sent if non-empty   |

`--doc-file` is derived from `--doc-name` by replacing `[^a-zA-Z0-9_-]` with
`_`. The output `.pdb` is expected at `<pluckerdir>/<doc-file>.pdb`.

### Tab 2: Spidering

| GUI Field        | CLI Flag               | Type | Default | Notes                      |
|------------------|------------------------|------|---------|----------------------------|
| Max Depth        | `--maxdepth=<n>`       | int  | 2       | Range: 1-999               |
| Stay on host     | `--stayonhost`         | flag | off     | Only sent when checked     |
| Depth-first      | `--depth-first`        | flag | off     | Only sent when checked     |
| URL Pattern      | `--staybelow=<url>`    | str  | (none)  | Only sent if non-empty     |
| User Agent       | `--user-agent=<str>`   | str  | (none)  | Only sent if non-empty     |

### Tab 3: Images

| GUI Field            | CLI Flag                | Type | Default | Notes                    |
|----------------------|-------------------------|------|---------|--------------------------|
| Color Depth (bpp)    | `--bpp=<n>`             | int  | 8       | 0, 1, 2, 4, 8, or 16    |
| No images            | `--noimages`            | flag | off     | Sent when bpp combo = 0  |
| Thumbnail Max Width  | `--maxwidth=<n>`        | int  | 150     |                          |
| Thumbnail Max Height | `--maxheight=<n>`       | int  | 250     |                          |
| Full-size Max Width  | `--alt-maxwidth=<n>`    | int  | 450     |                          |
| Full-size Max Height | `--alt-maxheight=<n>`   | int  | 800     |                          |
| Compression Limit    | *(not sent to Spider)*  | int  | 50      | GUI-only, not passed     |

**Note:** `imageCompressionLimit` is stored in our config but is NOT passed to
Spider.py. It is a GUI-only field with no corresponding CLI flag.

### Tab 4: Destination

| GUI Field    | CLI Flag                 | Type   | Default | Notes                   |
|--------------|--------------------------|--------|---------|-------------------------|
| Compression  | `--compression=<type>`   | string | zlib    | "zlib" or "DOC"         |
| Storage Mode | *(not sent to Spider)*   | string | ram     | Used by install phase   |
| Card Dir     | *(not sent to Spider)*   | string | (none)  | Used by install phase   |

Storage mode and card directory are handled by the C++ install phase, not by
Spider.py.

### Tab 5: Scheduling

Scheduling fields (updateEnabled, updateFrequency, updatePeriod, lastFetched)
are handled entirely in C++ and never passed to Spider.py.

### Always-sent Flag

| Flag            | Notes                                        |
|-----------------|----------------------------------------------|
| `--no-urlinfo`  | Always appended — disables URL info records   |

### Pluckerdir

| Flag                      | Notes                                    |
|---------------------------|------------------------------------------|
| `--pluckerdir=<dir>`      | Temporary directory per session          |

The output directory is always a per-PID temp dir:
`/tmp/qpilotsync-plucker-<pid>/`

## Reproducing QPilotSync Invocations

### Minimal (defaults)

```bash
export PYTHONPATH="/path/to/plucker/parser"
python3 /path/to/plucker/parser/PyPlucker/Spider.py \
    --home-url=https://example.com \
    --doc-name=Example \
    --doc-file=Example \
    --pluckerdir=/tmp/test-plucker \
    --maxdepth=2 \
    --bpp=8 \
    --maxwidth=150 \
    --maxheight=250 \
    --alt-maxwidth=450 \
    --alt-maxheight=800 \
    --compression=zlib \
    --no-urlinfo
```

### All spidering options enabled

```bash
python3 Spider.py \
    --home-url=https://planet.kde.org \
    --doc-name=PlanetKDE \
    --doc-file=PlanetKDE \
    --pluckerdir=/tmp/test-plucker \
    --maxdepth=3 \
    --bpp=8 \
    --maxwidth=150 \
    --maxheight=250 \
    --alt-maxwidth=450 \
    --alt-maxheight=800 \
    --compression=zlib \
    --stayonhost \
    --depth-first \
    --staybelow=https://planet.kde.org \
    --user-agent="Mozilla/5.0 QPilotSync" \
    --category=News \
    --no-urlinfo
```

### No images

```bash
python3 Spider.py \
    --home-url=https://example.com \
    --doc-name=TextOnly \
    --doc-file=TextOnly \
    --pluckerdir=/tmp/test-plucker \
    --maxdepth=2 \
    --bpp=8 \
    --maxwidth=150 \
    --maxheight=250 \
    --alt-maxwidth=450 \
    --alt-maxheight=800 \
    --compression=zlib \
    --noimages \
    --no-urlinfo
```

**Note:** When `--noimages` is set, our GUI still sends `--bpp`, `--maxwidth`,
etc. Spider.py should ignore image params when `--noimages` is active.

### DOC compression

```bash
python3 Spider.py \
    --home-url=https://example.com \
    --doc-name=DocComp \
    --doc-file=DocComp \
    --pluckerdir=/tmp/test-plucker \
    --maxdepth=1 \
    --bpp=1 \
    --maxwidth=150 \
    --maxheight=250 \
    --alt-maxwidth=450 \
    --alt-maxheight=800 \
    --compression=DOC \
    --no-urlinfo
```

### All bpp values

Test with each of: `--bpp=1`, `--bpp=2`, `--bpp=4`, `--bpp=8`, `--bpp=16`

### Edge cases to test

1. **URL with special characters:** `--home-url=https://example.com/path?q=a&b=c`
2. **Doc name with spaces/unicode:** `--doc-name=My Document` (doc-file will be `My_Document`)
3. **Depth 1 (single page):** `--maxdepth=1`
4. **Large depth:** `--maxdepth=50 --stayonhost` (should still terminate)
5. **Empty category:** omit `--category` entirely
6. **All image sizes at minimum:** `--maxwidth=1 --maxheight=1 --alt-maxwidth=1 --alt-maxheight=1`

## Expected Behavior

- Exit code 0 on success
- Output file at `<pluckerdir>/<doc-file>.pdb`
- Non-zero exit code on failure, with error details on stdout/stderr
- Should not hang beyond a reasonable timeout (our limit: 5 minutes)

## PYTHONPATH Requirement

QPilotSync sets `PYTHONPATH` to the directory *containing* the `PyPlucker/`
package directory (i.e., `parser/`). This is required for Spider.py's
`import PyPlucker` to resolve.

```
PYTHONPATH=/path/to/plucker/parser  python3  /path/to/plucker/parser/PyPlucker/Spider.py ...
```
