# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

"""Unified PMC data access layer."""

from __future__ import annotations

from typing import Any

import pandas as pd


class PmcDataCache:
    """Cache nested lookups from raw PMC data."""

    def __init__(self, raw_pmc_df: pd.DataFrame | dict) -> None:
        self._raw_pmc_df = raw_pmc_df
        self._cache: dict[str, Any] = {}

    def __getitem__(self, key: str) -> Any:  # noqa: ANN401
        if key not in self._cache:
            value = self._raw_pmc_df[key]
            if isinstance(value, pd.DataFrame):
                value = PmcDataCache(value)
            self._cache[key] = value
        return self._cache[key]

    def get(self, key: str, default: Any = None) -> Any:  # noqa: ANN401
        """Return cached value for *key*, or *default* on miss."""
        try:
            return self[key]
        except (KeyError, TypeError):
            return default

    def __contains__(self, key: object) -> bool:
        if isinstance(self._raw_pmc_df, pd.DataFrame):
            columns = self._raw_pmc_df.columns
            if isinstance(columns, pd.MultiIndex):
                return key in columns.get_level_values(0)
        return key in self._raw_pmc_df

    def has_column(self, table_key: str, col_name: str) -> bool:
        """Check whether *table_key* exists and contains *col_name*."""
        if table_key not in self:
            return False
        nested = self.get(table_key)
        if nested is None:
            return False
        try:
            return col_name in nested
        except TypeError:
            return False

    def __getattr__(self, name: str) -> Any:  # noqa: ANN401
        return getattr(self._raw_pmc_df, name)
