# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

import pytest
from collectives import COLLECTIVES, COLLECTIVES_WITH_OPS, COLLECTIVES_WITH_ROOT, MSG_SIZES
from test_runner import run_rccl_perf


# ---------------------------------------------------------------------------
# Group 14: -o all / -d all / -r all sweep modes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("collective", COLLECTIVES_WITH_OPS, ids=lambda c: c.name)
def test_ops_all(collective, msg_size, gpu_count):
    """All reduction ops in a single sweep (-o all)."""
    run_rccl_perf(collective.executable, [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-o", "all", "-d", "float"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("collective", COLLECTIVES, ids=lambda c: c.name)
def test_dtypes_all(collective, msg_size, gpu_count):
    """All datatypes in a single sweep (-d all)."""
    args = ["-t", str(gpu_count), "-g", "1",
            "-b", msg_size, "-e", msg_size, "-d", "all"]
    if collective.has_ops:
        args += ["-o", "sum"]
    if collective.has_root:
        args += ["-r", "0"]
    run_rccl_perf(collective.executable, args)


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("collective", COLLECTIVES_WITH_ROOT, ids=lambda c: c.name)
def test_root_all(collective, msg_size, gpu_count):
    """Rotate through all root ranks (-r all)."""
    args = ["-t", str(gpu_count), "-g", "1",
            "-b", msg_size, "-e", msg_size, "-r", "all", "-d", "float"]
    if collective.has_ops:
        args += ["-o", "sum"]
    run_rccl_perf(collective.executable, args)
