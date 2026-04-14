# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

import pytest
from collectives import MSG_SIZES
from test_runner import run_rccl_perf, run_rccl_mpi


# ---------------------------------------------------------------------------
# Group 2: Size sweep modes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_stepfactor(msg_size, gpu_count):
    """Multiplicative size stepping (-f)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", "1G", "-f", "2", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_stepbytes(msg_size, gpu_count):
    """Additive size stepping (-i)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-i", "1K", "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 3: Correctness checking
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("check_iters", ["1", "2"], ids=lambda c: f"check{c}")
def test_correctness_check(check_iters, msg_size, gpu_count):
    """Correctness verification logic (-c)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-c", check_iters,
        "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 4: Execution modes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_blocking(msg_size, gpu_count):
    """Blocking collective mode (-z 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-z", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_null_stream(msg_size, gpu_count):
    """NULL stream path (-y 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-y", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_parallel_init(msg_size, gpu_count):
    """Threaded NCCL/RCCL init (-p 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-p", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("mode", ["0", "1"], ids=["in_place", "out_of_place"])
def test_placement(mode, msg_size, gpu_count):
    """In-place vs out-of-place buffer paths (-O)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-O", mode, "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_hip_graph(msg_size, gpu_count):
    """HIP graph capture and replay (-G 2)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-G", "2", "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 5: Multi-GPU threading model
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_thread_per_gpu(msg_size, gpu_count):
    """-t <NGPUS> -g 1: one thread per GPU."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_multi_gpu_per_thread(msg_size, gpu_count):
    """-t 1 -g <NGPUS>: one thread, all GPUs."""
    run_rccl_perf("all_reduce_perf", [
        "-t", "1", "-g", str(gpu_count),
        "-b", msg_size, "-e", msg_size, "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 6: Rooted collectives
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_root_explicit(msg_size, gpu_count):
    """Explicit root rank (-r 0) for broadcast."""
    run_rccl_perf("broadcast_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-r", "0", "-d", "float"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_root_rotate(msg_size, gpu_count):
    """Rotate through all roots (-r all) for reduce."""
    run_rccl_perf("reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-r", "all", "-o", "sum", "-d", "float"])


# ---------------------------------------------------------------------------
# Group 7: Iteration control
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_custom_iters(msg_size, gpu_count):
    """Non-default iteration count (-n 5)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-n", "5", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_aggregated_iters(msg_size, gpu_count):
    """Aggregated/batched iterations (-m 2)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-m", "2", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_multi_cycle(msg_size, gpu_count):
    """Multi-cycle outer loop (-N 2)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-N", "2", "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 8: Output & reporting
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_cpu_time_report(msg_size, gpu_count):
    """CPU time reporting (-C 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-C", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_timestamps(msg_size, gpu_count):
    """Timestamp output (-S 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-S", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("avg_mode", ["0", "2", "3"],
                         ids=["avg_rank0", "avg_min", "avg_max"])
def test_average_mode(avg_mode, msg_size, gpu_count):
    """Average reporting modes (-a)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-a", avg_mode, "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_json_output(msg_size, gpu_count, tmp_path):
    """JSON file output (-J)."""
    outfile = str(tmp_path / "rccl_test.json")
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-J", outfile, "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_memory_report(msg_size, gpu_count):
    """Memory usage reporting (-M 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-M", "1", "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 9: RCCL-specific features
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
@pytest.mark.parametrize("mem_type", ["fine", "host", "managed"],
                         ids=lambda m: f"mem_{m}")
def test_memory_type(mem_type, msg_size, gpu_count):
    """Non-default memory types (-Y)."""
    env = {"HSA_FORCE_FINE_GRAIN_PCIE": "1"} if mem_type == "fine" else None
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-Y", mem_type,
        "-d", "float", "-o", "sum"], env_overrides=env)


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_rotating_tensor(msg_size, gpu_count):
    """Rotating tensor pattern (-E 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-E", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_rccl_reporter_csv(msg_size, gpu_count, tmp_path):
    """RCCL Reporter CSV output (-Z csv -X file)."""
    outfile = str(tmp_path / "rccl_report.csv")
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-Z", "csv", "-X", outfile,
        "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 10: Buffer registration & algo reporting
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_local_register(msg_size, gpu_count):
    """Local buffer registration (-R 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-R", "1", "-d", "float", "-o", "sum"])


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_algo_proto_channels(msg_size, gpu_count):
    """Algo/proto/channel reporting (-A 1)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-A", "1", "-d", "float", "-o", "sum"])


# ---------------------------------------------------------------------------
# Group 11: Environment variables
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_env_device_override(msg_size):
    """NCCL_TESTS_DEVICE override (single GPU, does not use gpu_count fixture)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", "1", "-g", "1",
        "-b", msg_size, "-e", msg_size, "-d", "float", "-o", "sum"],
        env_overrides={"NCCL_TESTS_DEVICE": "0"})


@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_env_min_bw(msg_size, gpu_count):
    """NCCL_TESTS_MIN_BW pass/fail threshold."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-d", "float", "-o", "sum"],
        env_overrides={"NCCL_TESTS_MIN_BW": "0.001"})


# ---------------------------------------------------------------------------
# Group 12: MPI mode
# ---------------------------------------------------------------------------

@pytest.mark.mpi
@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_mpi_basic(msg_size, gpu_count):
    """Basic MPI launch with parallel init."""
    run_rccl_mpi("all_reduce_perf", gpu_count, [
        "-t", "1", "-g", "1",
        "-b", msg_size, "-e", msg_size, "-p", "1", "-d", "float", "-o", "sum"])


@pytest.mark.mpi
@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_mpi_root_rotate(msg_size, gpu_count):
    """MPI with root rotation."""
    run_rccl_mpi("broadcast_perf", gpu_count, [
        "-t", "1", "-g", "1",
        "-b", msg_size, "-e", msg_size, "-r", "all", "-d", "float"])


# ---------------------------------------------------------------------------
# Group 13: Internal timeout
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("msg_size", MSG_SIZES, ids=lambda s: s)
def test_internal_timeout(msg_size, gpu_count):
    """RCCL-Tests internal timeout mechanism (-T)."""
    run_rccl_perf("all_reduce_perf", [
        "-t", str(gpu_count), "-g", "1",
        "-b", msg_size, "-e", msg_size, "-T", "30", "-d", "float", "-o", "sum"])
