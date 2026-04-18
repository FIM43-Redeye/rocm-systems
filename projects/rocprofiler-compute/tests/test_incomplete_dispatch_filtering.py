# Copyright (c) Advanced Micro Devices, Inc.
# SPDX-License-Identifier:  MIT

import numpy as np
import pandas as pd
import pytest

from utils.utils_analysis import nullify_incomplete_dispatch_counters


def make_multilevel_df(data: dict) -> pd.DataFrame:
    """Create a MultiIndex DataFrame from (level, column) -> values tuples."""
    df = pd.DataFrame(data)
    df.columns = pd.MultiIndex.from_tuples(df.columns)
    return df


def make_pmc_df(
    kernel_names: list[str],
    dispatch_ids: list[int],
    counter_a: list,
    counter_b: list,
) -> pd.DataFrame:
    """Helper to create a typical pmc_perf multi-index DataFrame."""
    n = len(kernel_names)
    data = {
        ("pmc_perf", "Dispatch_ID"): dispatch_ids,
        ("pmc_perf", "GPU_ID"): [0] * n,
        ("pmc_perf", "Grid_Size"): [1024] * n,
        ("pmc_perf", "Workgroup_Size"): [64] * n,
        ("pmc_perf", "LDS_Per_Workgroup"): [32] * n,
        ("pmc_perf", "Scratch_Per_Workitem"): [0] * n,
        ("pmc_perf", "Arch_VGPR"): [16] * n,
        ("pmc_perf", "Accum_VGPR"): [0] * n,
        ("pmc_perf", "SGPR"): [32] * n,
        ("pmc_perf", "Kernel_Name"): kernel_names,
        ("pmc_perf", "Start_Timestamp"): list(range(1000, 1000 + n * 200, 200)),
        ("pmc_perf", "End_Timestamp"): list(range(1500, 1500 + n * 200, 200)),
        ("pmc_perf", "Kernel_ID"): [1] * n,
        ("pmc_perf", "Counter_A"): counter_a,
        ("pmc_perf", "Counter_B"): counter_b,
    }
    return make_multilevel_df(data)


class TestNullifyIncompleteDispatchCounters:
    """Tests for nullify_incomplete_dispatch_counters()."""

    def test_basic_nan_nullification(self):
        """Rows with any NaN in counter columns get ALL counter values set to NaN."""
        df = make_pmc_df(
            kernel_names=["kernel_x", "kernel_y", "kernel_y"],
            dispatch_ids=[0, 1, 2],
            counter_a=[100.0, 10.0, 20.0],
            counter_b=[np.nan, 20.0, 30.0],
        )

        result = nullify_incomplete_dispatch_counters(df)

        # Row 0 had NaN in Counter_B -> both counters should be NaN
        assert np.isnan(result[("pmc_perf", "Counter_A")].iloc[0])
        assert np.isnan(result[("pmc_perf", "Counter_B")].iloc[0])

        # Rows 1 and 2 had no NaN -> values preserved
        assert result[("pmc_perf", "Counter_A")].iloc[1] == 10.0
        assert result[("pmc_perf", "Counter_B")].iloc[1] == 20.0
        assert result[("pmc_perf", "Counter_A")].iloc[2] == 20.0
        assert result[("pmc_perf", "Counter_B")].iloc[2] == 30.0

    def test_no_nan_passthrough(self):
        """DataFrame with no NaN values passes through unchanged."""
        df = make_pmc_df(
            kernel_names=["kernel_a", "kernel_b"],
            dispatch_ids=[0, 1],
            counter_a=[100.0, 200.0],
            counter_b=[300.0, 400.0],
        )

        result = nullify_incomplete_dispatch_counters(df)

        pd.testing.assert_frame_equal(result, df)

    def test_non_multiindex_raises(self):
        """Non-MultiIndex DataFrame raises ValueError."""
        df = pd.DataFrame({"A": [1, 2, 3], "B": [4, 5, 6]})
        with pytest.raises(ValueError):
            nullify_incomplete_dispatch_counters(df)

    def test_multiple_collection_levels(self):
        """Works correctly with multiple collection levels."""
        data = {
            ("pmc_perf", "Dispatch_ID"): [0, 1],
            ("pmc_perf", "Kernel_Name"): ["k1", "k1"],
            ("pmc_perf", "Start_Timestamp"): [1000, 1200],
            ("pmc_perf", "End_Timestamp"): [1500, 1700],
            ("pmc_perf", "Counter_A"): [100.0, np.nan],
            ("pmc_perf", "Counter_B"): [200.0, 300.0],
            ("SQ_ACCUM", "Dispatch_ID"): [0, 1],
            ("SQ_ACCUM", "Kernel_Name"): ["k1", "k1"],
            ("SQ_ACCUM", "Counter_C"): [np.nan, 400.0],
        }
        df = make_multilevel_df(data)

        result = nullify_incomplete_dispatch_counters(df)

        # pmc_perf level: row 1 has NaN in Counter_A -> both counters NaN
        assert not np.isnan(result[("pmc_perf", "Counter_A")].iloc[0])
        assert np.isnan(result[("pmc_perf", "Counter_A")].iloc[1])
        assert np.isnan(result[("pmc_perf", "Counter_B")].iloc[1])

        # SQ_ACCUM level: row 0 has NaN in Counter_C -> Counter_C NaN
        assert np.isnan(result[("SQ_ACCUM", "Counter_C")].iloc[0])
        assert result[("SQ_ACCUM", "Counter_C")].iloc[1] == 400.0

    def test_original_df_not_modified(self):
        """The original DataFrame is not modified (copy semantics)."""
        df = make_pmc_df(
            kernel_names=["kernel_x"],
            dispatch_ids=[0],
            counter_a=[100.0],
            counter_b=[np.nan],
        )

        original_val = df[("pmc_perf", "Counter_A")].iloc[0]
        nullify_incomplete_dispatch_counters(df)
        assert df[("pmc_perf", "Counter_A")].iloc[0] == original_val
