# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

import pytest
from collectives import OPS, MSG_SIZES
from test_runner import run_rccl_perf


# ---------------------------------------------------------------------------
# Group 15: Parametrized individual reduction ops
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("op", OPS, ids=lambda o: o)
def test_op(op, msg_size, gpu_count):
    """Each reduction op individually — granular CI reporting."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size,
        "-o", op, "-d", "float",
    ])
