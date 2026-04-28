#!/usr/bin/env python3
"""Stub spider — writes a fixed .pdb and exits 0."""
import argparse
import os
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--home-url")
    parser.add_argument("--doc-name")
    parser.add_argument("--doc-file")
    parser.add_argument("--pluckerdir")
    args, _ = parser.parse_known_args()
    if not args.doc_file or not args.pluckerdir:
        sys.exit(2)
    out = os.path.join(args.pluckerdir, args.doc_file + ".pdb")
    with open(out, "wb") as f:
        f.write(b"PLUCKER_TEST")
    print(f"wrote {out}")
    sys.exit(0)

if __name__ == "__main__":
    main()
