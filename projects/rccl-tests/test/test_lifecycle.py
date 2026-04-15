# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

import pytest
from collectives import COLLECTIVES, MSG_SIZES
from test_runner import run_rccl_perf, run_rccl_mpi


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("collective", COLLECTIVES, ids=lambda c: c.name)
def test_lifecycle_single(collective, msg_size, gpu_count):
    """Each binary launches and exits cleanly. Arch-gated where needed."""
    args = ["-t", str(gpu_count), "-g", "1",
            "-b", msg_size, "-e", msg_size, "-d", "float"]
    if collective.has_ops:
        args += ["-o", "sum"]
    if collective.has_root:
        args += ["-r", "0"]
    run_rccl_perf(collective.executable, args)


@pytest.mark.mpi
@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("collective", COLLECTIVES, ids=lambda c: c.name)
def test_lifecycle_mpi(collective, msg_size, gpu_count):
    """MPI launch path for each binary."""
    args = ["-t", "1", "-g", "1",
            "-b", msg_size, "-e", msg_size, "-d", "float"]
    if collective.has_ops:
        args += ["-o", "sum"]
    if collective.has_root:
        args += ["-r", "0"]
    run_rccl_mpi(collective.executable, gpu_count, args)
