# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

import pytest
from collectives import DATATYPES, MSG_SIZES
from test_runner import run_rccl_perf


# ---------------------------------------------------------------------------
# Group 16: Parametrized individual datatypes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("dtype", DATATYPES, ids=lambda d: d)
def test_dtype(dtype, msg_size, gpu_count):
    """Each datatype individually — granular CI reporting."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size,
        "-o", "sum", "-d", dtype,
    ])
