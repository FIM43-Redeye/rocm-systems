# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

import numpy as np
import pandas as pd
import pytest

from utils.metrics.debug_row_tracker import (
    _collect_debug_column_data,
    _extract_column_data,
)
from utils.metrics.metric_evaluator import MetricEvaluator
from utils.metrics.pmc_data_cache import PmcDataCache


def _make_pmc_dict() -> dict[str, pd.DataFrame]:
    """Build a minimal PMC dictionary with a single 'pmc_perf' table."""
    return {
        "pmc_perf": pd.DataFrame({
            "SQ_WAVES": [100, 200, 150],
            "GRBM_GUI_ACTIVE": [1000, 2000, 1500],
        })
    }


def _make_pmc_multiindex_df() -> pd.DataFrame:
    """Build a MultiIndex DataFrame equivalent to the dict fixture."""
    sub_df = pd.DataFrame({
        "SQ_WAVES": [100, 200, 150],
        "GRBM_GUI_ACTIVE": [1000, 2000, 1500],
    })
    return pd.concat({"pmc_perf": sub_df}, axis=1)


def test_pmc_data_cache_dict_input() -> None:
    """Verify dict input yields a nested PmcDataCache with correct series values."""
    cache = PmcDataCache(_make_pmc_dict())

    nested = cache["pmc_perf"]
    assert isinstance(nested, PmcDataCache)

    series = nested["SQ_WAVES"]
    assert isinstance(series, pd.Series)
    assert list(series) == [100, 200, 150]


def test_pmc_data_cache_dataframe_input() -> None:
    """Verify MultiIndex DataFrame input behaves identically to dict input."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    nested = cache["pmc_perf"]
    assert isinstance(nested, PmcDataCache)

    series = nested["SQ_WAVES"]
    assert isinstance(series, pd.Series)
    assert list(series) == [100, 200, 150]


@pytest.mark.parametrize("invalid_input", [None, [1, 2], 42, "not-a-table"])
def test_pmc_data_cache_rejects_invalid_input(invalid_input: object) -> None:
    """Construction rejects unsupported input types with TypeError."""
    with pytest.raises(TypeError, match="unsupported raw_pmc_df type"):
        PmcDataCache(invalid_input)  # type: ignore[arg-type]


def test_pmc_data_cache_level1_cache_hit() -> None:
    """Repeated top-level lookups must return the same cached object."""
    cache = PmcDataCache(_make_pmc_dict())

    first = cache["pmc_perf"]
    second = cache["pmc_perf"]
    assert first is second


def test_pmc_data_cache_level2_cache_hit() -> None:
    """Repeated column lookups on a nested cache must return the same object."""
    cache = PmcDataCache(_make_pmc_dict())

    nested = cache["pmc_perf"]
    first = nested["SQ_WAVES"]
    second = nested["SQ_WAVES"]
    assert first is second


def test_pmc_data_cache_multiindex_level2_cache_hit() -> None:
    """MultiIndex column lookups on a nested cache must return the same object."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    nested = cache["pmc_perf"]
    first = nested["SQ_WAVES"]
    second = nested["SQ_WAVES"]
    assert first is second


def test_pmc_data_cache_contains_and_get() -> None:
    """'in' and 'get' mirror dict semantics for missing keys."""
    cache = PmcDataCache(_make_pmc_dict())

    assert "pmc_perf" in cache
    assert "nonexistent" not in cache

    assert isinstance(cache.get("pmc_perf"), PmcDataCache)
    assert cache.get("nonexistent") is None
    assert cache.get("nonexistent", "fallback") == "fallback"


def test_pmc_data_cache_has_column_present() -> None:
    """has_column returns True when both table and column exist."""
    cache = PmcDataCache(_make_pmc_dict())
    assert cache.has_column("pmc_perf", "SQ_WAVES") is True


def test_pmc_data_cache_has_column_missing_table() -> None:
    """has_column returns False when the table name does not exist."""
    cache = PmcDataCache(_make_pmc_dict())
    assert cache.has_column("nonexistent", "SQ_WAVES") is False


def test_pmc_data_cache_has_column_missing_column() -> None:
    """has_column returns False when the column name does not exist in the table."""
    cache = PmcDataCache(_make_pmc_dict())
    assert cache.has_column("pmc_perf", "NONEXISTENT") is False


def test_pmc_data_cache_has_column_with_scalar_nested() -> None:
    """has_column returns False when the table key resolves to a scalar value."""
    raw = {"version": 42, "pmc_perf": pd.DataFrame({"A": [1]})}
    cache = PmcDataCache(raw)
    assert cache.has_column("version", "anything") is False
    assert cache.has_column("pmc_perf", "A") is True


def test_pmc_data_cache_multiindex_contains() -> None:
    """Containment checks level-0 table labels for MultiIndex DataFrame input."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    assert "pmc_perf" in cache
    assert "nonexistent" not in cache


def test_pmc_data_cache_multiindex_has_column_present() -> None:
    """has_column returns True for existing MultiIndex table/column combinations."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    assert cache.has_column("pmc_perf", "SQ_WAVES") is True


def test_pmc_data_cache_multiindex_has_column_missing() -> None:
    """has_column returns False for missing MultiIndex tables or columns."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    assert cache.has_column("pmc_perf", "NONEXISTENT") is False
    assert cache.has_column("nonexistent", "SQ_WAVES") is False


def test_pmc_data_cache_getitem_missing_key() -> None:
    """Accessing a missing key via [] raises KeyError."""
    cache = PmcDataCache(_make_pmc_dict())
    with pytest.raises(KeyError):
        cache["nonexistent"]


def test_pmc_data_cache_get_type_error() -> None:
    """get() with a non-string key returns None or the supplied default."""
    cache = PmcDataCache(_make_pmc_dict())
    nested = cache["pmc_perf"]
    assert nested.get([1, 2]) is None
    assert nested.get([1, 2], "fallback") == "fallback"


def test_pmc_data_cache_scalar_value_not_wrapped() -> None:
    """Scalar values stored alongside DataFrames are returned unwrapped."""
    raw = {"version": 42, "pmc_perf": pd.DataFrame({"A": [1]})}
    cache = PmcDataCache(raw)

    assert cache["version"] == 42
    assert not isinstance(cache["version"], PmcDataCache)


def test_pmc_data_cache_with_metric_evaluator() -> None:
    """PmcDataCache integrates with MetricEvaluator for expression evaluation."""
    cache = PmcDataCache(_make_pmc_dict())

    evaluator = MetricEvaluator(cache, {}, {})
    result = evaluator.eval_expression("to_avg(raw_pmc_df['pmc_perf']['SQ_WAVES'])")

    assert result != "N/A"
    assert np.isclose(result, 150.0)


# ---------------------------------------------------------------------------
# debug_row_tracker integration with PmcDataCache
# ---------------------------------------------------------------------------


def test_collect_debug_column_data_matches_bracketed_counter() -> None:
    """The debug regex captures bracketed counter names like TCC_HIT[0]."""
    cache = PmcDataCache({"pmc_perf": pd.DataFrame({"TCC_HIT[0]": [10, 20, 30]})})
    row_expr = "to_sum(raw_pmc_df['pmc_perf']['TCC_HIT[0]'])"

    rows_to_print, _global_width = _collect_debug_column_data(row_expr, cache)

    assert len(rows_to_print) == 1
    label, column_data = rows_to_print[0]
    assert label == "raw_pmc_df['pmc_perf']['TCC_HIT[0]']"
    assert column_data == [10, 20, 30]


def test_extract_column_data_multiindex_dataframe() -> None:
    """MultiIndex DataFrame input yields list values via _extract_column_data."""
    cache = PmcDataCache(_make_pmc_multiindex_df())

    result = _extract_column_data("pmc_perf", "SQ_WAVES", cache)

    assert result == [100, 200, 150]


def test_extract_column_data_missing_table_returns_none() -> None:
    """Missing table_key returns None even when col_name shadows a top-level key."""
    cache = PmcDataCache({
        "pmc_perf": pd.DataFrame({"SQ_WAVES": [1, 2, 3]}),
        "shadow": pd.DataFrame({"X": [9, 9, 9]}),
    })

    # `shadow` is a top-level table; the rewrite must not fall through to it
    # when the requested table_key does not exist.
    assert _extract_column_data("missing_table", "shadow", cache) is None
