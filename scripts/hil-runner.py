#!/usr/bin/env python3
"""Thin executable wrapper around HIL validation, row, RH3, MA1, and SA1 CLI."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hil.cli import main  # noqa: E402 - after sys.path setup

if __name__ == "__main__":
    sys.exit(main())
