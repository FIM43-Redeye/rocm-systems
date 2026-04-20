# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

"""Unified PMC data access layer."""

from __future__ import annotations

from typing import Any

import pandas as pd


class PmcDataCache:
    """Wrap raw PMC data behind a uniform table-of-tables view.

    Accepted top-level inputs:

    * ``dict`` — typically ``dict[str, pd.DataFrame]``; scalar values
      (e.g. ``{"version": 42}``) are returned unwrapped.
    * MultiIndex ``pd.DataFrame`` — production input; level-0 labels become
      table keys.
    * Flat ``pd.DataFrame`` — only used by tests/migrations; columns become
      table keys with ``pd.Series`` values.

    All backing-type detection happens once at construction time. Lookups via
    ``[]`` are identity-preserving across repeated calls (the same wrapper
    object is returned for a given key), and nested ``DataFrame`` values are
    lazily wrapped in their own ``PmcDataCache``.
    """

    def __init__(self, raw_pmc_df: pd.DataFrame | dict) -> None:
        self._tables: dict[str, Any] = self._normalize(raw_pmc_df)
        self._wrappers: dict[str, PmcDataCache] = {}

    @staticmethod
    def _normalize(raw: pd.DataFrame | dict) -> dict[str, Any]:
        """Reduce supported inputs to a flat ``dict[str, Any]`` of tables."""
        if isinstance(raw, pd.DataFrame):
            if isinstance(raw.columns, pd.MultiIndex):
                top_level_keys = raw.columns.get_level_values(0).unique()
                return {key: raw[key] for key in top_level_keys}
            # Flat DataFrame: production never reaches this branch; kept for
            # tests and migration paths that pass a Series-per-column frame.
            return {column: raw[column] for column in raw.columns}
        if isinstance(raw, dict):
            return dict(raw)
        raise TypeError(f"unsupported raw_pmc_df type: {type(raw).__name__}")

    def __getitem__(self, key: str) -> Any:  # noqa: ANN401
        if key in self._wrappers:
            return self._wrappers[key]
        value = self._tables[key]
        if isinstance(value, pd.DataFrame):
            wrapped = PmcDataCache(value)
            self._wrappers[key] = wrapped
            return wrapped
        return value

    def __contains__(self, key: object) -> bool:
        return key in self._tables

    def get(self, key: str, default: Any = None) -> Any:  # noqa: ANN401
        """Return the value for *key*, or *default* when missing."""
        try:
            return self[key]
        except (KeyError, TypeError):
            return default

    def has_column(self, table_key: str, col_name: str) -> bool:
        """Check whether *table_key* exists and contains *col_name*."""
        if table_key not in self._tables:
            return False
        try:
            nested = self[table_key]
        except (KeyError, TypeError):
            return False
        return isinstance(nested, PmcDataCache) and col_name in nested
