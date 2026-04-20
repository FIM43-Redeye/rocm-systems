# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

"""Unified PMC data access layer."""

from __future__ import annotations

from typing import Any

import pandas as pd


class PmcDataCache:
    """Wrap a 2-level MultiIndex PMC DataFrame.

    Assumes ``raw_pmc_df.columns`` is a 2-level ``pd.MultiIndex`` whose level-0
    labels name tables (e.g. ``'pmc_perf'``) and whose level-1 labels name
    counters. ``cache['pmc_perf']`` returns an inner ``PmcDataCache`` over the
    flat sub-DataFrame; ``cache['pmc_perf']['SQ_WAVES']`` returns the underlying
    ``pd.Series``. Lookups are identity preserving: repeated calls return the
    same wrapper / ``Series`` object.
    """

    def __init__(self, raw_pmc_df: pd.DataFrame, *, _flat: bool = False) -> None:
        if not hasattr(raw_pmc_df, "columns"):
            raise TypeError(
                f"unsupported raw_pmc_df type: {type(raw_pmc_df).__name__}"
            )
        self._df = raw_pmc_df
        self._cache: dict[str, Any] = (
            {col: raw_pmc_df[col] for col in raw_pmc_df.columns}
            if _flat
            else {
                key: PmcDataCache(raw_pmc_df[key], _flat=True)
                for key in raw_pmc_df.columns.get_level_values(0).unique()
            }
        )

    def __getitem__(self, key: str) -> Any:  # noqa: ANN401
        return self._cache[key]

    def __contains__(self, key: object) -> bool:
        return key in list(self._cache)

    def get(self, key: str, default: Any = None) -> Any:  # noqa: ANN401
        """Return the value for *key*, or *default* when missing."""
        if key in self:
            return self[key]
        return default

    def has_column(self, table_key: str, col_name: str) -> bool:
        """Check whether *table_key* exists and contains *col_name*."""
        return (table_key, col_name) in self._df.columns
