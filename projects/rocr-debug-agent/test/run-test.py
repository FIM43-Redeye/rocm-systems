import os
import re
import select
import sys
import tempfile
import unittest.mock
from subprocess import CalledProcessError, PIPE, Popen, TimeoutExpired, run
import time
import hashlib
import logging
import shutil
import signal

DEFAULT_TIMEOUT = 60


def run_and_communicate(
    test_name,
    args="0",
    debug_agent_options="",
    timeout=DEFAULT_TIMEOUT,
):
    # Prepare command
    program = "./rocm-debug-agent-test"
    cmd = [program]
    if isinstance(args, (list, tuple)):
        cmd += [str(a) for a in args]
    else:
        cmd.append(str(args))
    with unittest.mock.patch.dict(
        os.environ,
        (
            {"ROCM_DEBUG_AGENT_OPTIONS": debug_agent_options}
            if debug_agent_options
            else {}
        ),
    ):
        p = Popen(cmd, stdout=PIPE, stderr=PIPE)

        def handle_failure(reason):
            _log = logging.getLogger("rocm-debug-agent-test")
            _log.info("Test %s FAIL: %s", test_name, reason)
            p.kill()
            try:
                # Kill first, then communicate to flush any remaining output
                output, err = p.communicate()
            except Exception:
                output, err = b"", b""
            combined = (output.decode("utf-8").rstrip() + "\n" + err.decode("utf-8").rstrip()).strip()
            if combined:
                _log.info("output:\n%s", combined)
            return None, None, False

        try:
            output, err = p.communicate(timeout=timeout)
        except TimeoutExpired:
            return handle_failure("Timeout reached during communicate.")
        except Exception:
            return handle_failure("Unknown exception.")

        out_str = output.decode("utf-8")
        err_str = err.decode("utf-8")
        _log = logging.getLogger("rocm-debug-agent-test")
        combined = (out_str.rstrip() + "\n" + err_str.rstrip()).strip()
        if combined:
            _log.info("output:\n%s", combined)
        return out_str, err_str, True


def check_errors(check_list, out_str, err_str):
    _log = logging.getLogger("rocm-debug-agent-test")
    all_strings_found = True
    for i, check_str in enumerate(check_list, start=1):
        if not (check_str.search(err_str)):
            all_strings_found = False
            _log.info("Pattern %d/%d NOT found: %s", i, len(check_list), check_str.pattern)

    return all_strings_found


def filter_warnings(err_str):
    """Filter out warnings wich are expected on some archs."""
    return "\n".join(
        [
            line
            for line in err_str.split("\n")
            if not (
                "Precise memory not supported for all the agents" in line
                or "architecture not supported" in line
                or "Warning: Resource leak detected" in line
                or "rocm-dbgapi: warning: Cannot locate the amdgpu.ids file." in line
            )
        ]
    )


# set up
if len(sys.argv) != 2:
    raise Exception(
        "ERROR: Please specify test binary location. For example: $python3.6 run_test.py ./build"
    )
else:
    test_binary_directory = sys.argv[1]
    agent_library_directory = os.path.abspath(test_binary_directory) + "/.."
    if not "LD_LIBRARY_PATH" in os.environ:
        os.environ["LD_LIBRARY_PATH"] = agent_library_directory
    else:
        os.environ["LD_LIBRARY_PATH"] += ":" + agent_library_directory
    os.environ["HSA_TOOLS_LIB"] = "librocm-debug-agent.so.2"
    os.chdir(test_binary_directory)

    # Set up file-only logging for diagnostic output (stdout/stderr dumps).
    log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "run-test.log")
    log = logging.getLogger("rocm-debug-agent-test")
    log.setLevel(logging.DEBUG)
    _fh = logging.FileHandler(log_path, mode="w")
    _fh.setFormatter(logging.Formatter("%(message)s"))
    log.addHandler(_fh)
    log.propagate = False
    print(f"Diagnostic log: {log_path}")

    # pre test to check if librocm-debug-agent.so.2 can be found
    out_str, err_str, success = run_and_communicate(
        test_name="0: default", args="0"
    )

    if success and filter_warnings(err_str):
        print(err_str)
        if '"librocm-debug-agent.so.2" failed to load' in err_str:
            print(
                "ERROR: Cannot find librocm-debug-agent.so.2, please set its location with environment variable LD_LIBRARY_PATH"
            )
        sys.exit(1)


def test_baseline():
    out_str, err_str, success = run_and_communicate(
        test_name="0: default", args="0"
    )

    if not success:
        return False

    # Only log but not throw for err_str, since debug build has print
    # out that could be ignored
    if filter_warnings(err_str):
        logging.getLogger("rocm-debug-agent-test").info("output:\n%s", err_str)

    return True


def test_snapshot_code_object():
    out_str, err_str, success = run_and_communicate(
        test_name="3: snapshot code object on load", args="3"
    )
    if not success:
        return False

    found_error = False

    # If the debug agent did not capture the code object on load, it should
    # not be able to open it on exception, leading to the following warning:
    #
    #    rocm-debug-agent: warning: elf_getphdrnum failed for `memory://226967#offset=0x1651d4f0&size=3456'
    #    rocm-debug-agent: warning: could not open code_object_1
    if "could not open code_object" in err_str:
        found_error = True

    # If the code object was not properly loaded, we should not have any
    # disassembly in the output
    if (
        "Disassembly:\n" not in err_str
        and "Disassembly for function kernel_abort:\n" not in err_str
    ):
        found_error = True

    if found_error:
        _log = logging.getLogger("rocm-debug-agent-test")
        _log.info("Snapshot code object test failed: code_object error or missing disassembly")

    return not found_error


def _file_checksum(path: str, algo: str = "sha256") -> str:
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def _has_symbol_with_readelf(path: str, symbol: str) -> bool:
    # Use readelf -s (symbol table). Return True if symbol name appears.
    try:
        res = run(["readelf", "-sW", path], stdout=PIPE, stderr=PIPE, check=True)
        out = res.stdout.decode(errors="ignore")
        return symbol in out
    except (CalledProcessError):
        return False


def test_save_code_objects():
    if not shutil.which("readelf"):
        logging.getLogger("rocm-debug-agent-test").info(
            "Tool readelf not found, skipping test_save_code_objects"
        )
        unsupported_tests.append(test_save_code_objects)
        return True

    with tempfile.TemporaryDirectory() as tmpdir:
        run_and_communicate(
            test_name="4: save code objects",
            args="4",
            debug_agent_options=f"--save-code-objects={tmpdir} --load-all-code-objects",
        )

        try:
            code_objects = os.listdir(tmpdir)
        except FileNotFoundError as e:
            raise FileNotFoundError(
                f"Temporary directory not found: {tmpdir}"
            ) from e

        if len(code_objects) == 0:
            logging.getLogger("rocm-debug-agent-test").info(
                "No code object found in %s", tmpdir
            )
            return False

        # Filter to files that contain the target symbol in their
        # symbol table.
        full_paths = [
            os.path.join(tmpdir, f)
            for f in code_objects
            if os.path.isfile(os.path.join(tmpdir, f))
        ]
        with_symbol = []
        symbol_to_find = "saved_test_kernel"
        for path in full_paths:
            if _has_symbol_with_readelf(path, symbol_to_find):
                with_symbol.append(path)

        _log = logging.getLogger("rocm-debug-agent-test")
        if len(with_symbol) != 2:
            _log.info(
                "Expected exactly 2 code objects containing symbol"
                " '%s', found %d", symbol_to_find, len(with_symbol)
            )
            _log.info(
                "All saved files:\n\t%s", "\n\t".join(code_objects)
            )
            _log.info(
                "Files with symbol:\n\t%s",
                "\n\t".join(os.path.basename(p) for p in with_symbol)
            )
            return False

        # Compare contents via checksum.
        checksums = [(_file_checksum(p), p) for p in with_symbol]
        unique_sums = {cs for cs, _ in checksums}
        if len(unique_sums) != 1:
            _log.info(
                "The two code objects containing the symbol do not"
                " have identical contents"
            )
            for cs, pth in checksums:
                _log.info("%s -> %s", os.path.basename(pth), cs)
            return False

        return True


def test_output_redirection():
    check_list = [
        re.compile(s)
        for s in [
            "s0:",
            "v0:",
            "0x0000: 22222222 11111111",  # First uint64_t in LDS is '1111111122222222'
            "Disassembly for function vector_add_assert_trap\\(int\\*, int\\*, int\\*\\)",
        ]
    ]

    with tempfile.TemporaryDirectory() as tmpdir:
        with unittest.mock.patch.dict(
            os.environ, {"ROCM_DEBUG_AGENT_OPTIONS": f"-o {tmpdir}/output_log.txt"}
        ):

            # Start process, ignore what function return since everything
            # is written to the file
            run_and_communicate(
                "5: -o option",
                args="1",
                debug_agent_options=f"-o {tmpdir}/output_log.txt",
            )

            # Read the output log
            with open(f"{tmpdir}/output_log.txt", "r") as f:
                log_contents = f.read()

            all_output_string_found = True
            _log = logging.getLogger("rocm-debug-agent-test")
            for check_str in check_list:
                if not check_str.search(log_contents):
                    all_output_string_found = False
                    _log.info('"%s" Not Found in output_log.txt.', check_str.pattern)

            if not all_output_string_found:
                _log.info("Full output log contents:\n%s", log_contents)

            return all_output_string_found


def test_sigquit():
    check_list = [
        re.compile(s)
        for s in [
            "s0:",
            "v0:",
            "Disassembly for function sigquit_kern\\(int\\*\\)",
        ]
    ]

    LOOP_TIMEOUT = DEFAULT_TIMEOUT  # seconds

    p = Popen(["./rocm-debug-agent-test", "6"], stdout=PIPE, stderr=PIPE)

    kernel_started = False
    wave_seen = False
    timeout_seen = False

    consumed_out = []
    consumed_err = []

    deadline = time.monotonic() + LOOP_TIMEOUT
    streams_to_read = [p.stdout, p.stderr]

    while time.monotonic() < deadline and streams_to_read:

        rlist, _, _ = select.select(streams_to_read, [], [], 1)
        if not rlist:
            continue

        for r in rlist:
            line = r.readline()

            if line == b"":
                # Reading "" means that we reached EOF on this stream.
                # Remove it from the streams of interest so every stream
                # can be fully flushed before we exit the loop.
                del streams_to_read[streams_to_read.index(r)]
                continue

            s = line.decode("utf-8")
            if r is p.stdout:
                consumed_out.append(s)
            else:
                consumed_err.append(s)

            if not kernel_started and "Kernel started" in s:
                kernel_started = True
                os.kill(p.pid, signal.SIGQUIT)
                # We give our program LOOP_TIMEOUT secs to start the kernel.
                # Once we know that the kernel is running, we give it
                # an extra LOOP_TIMEOUT seconds to process SIGQUIT.
                deadline = deadline + LOOP_TIMEOUT

            if kernel_started:
                if s.lstrip().startswith(
                    "Disassembly for function sigquit_kern(int*):"
                ):
                    wave_seen = True
                    break
            if "Timeout reached. Exiting." in s:
                timeout_seen = True
        if wave_seen or timeout_seen:
            break

    p.terminate()
    try:
        output, err = p.communicate(timeout=3)
    except TimeoutExpired:
        logging.getLogger("rocm-debug-agent-test").info(
            "Timeout reached during final communicate."
        )
        output, err = b"", b""
    except Exception:
        logging.getLogger("rocm-debug-agent-test").info(
            "Unexpected exception during final communicate."
        )
        output, err = b"", b""

    out_str = "".join(consumed_out) + output.decode("utf-8")
    err_str = "".join(consumed_err) + err.decode("utf-8")
    _log = logging.getLogger("rocm-debug-agent-test")

    combined = (out_str.rstrip() + "\n" + err_str.rstrip()).strip()

    if not kernel_started:
        _log.info("Timeout waiting for 'Kernel started'. Terminating process.")
        if combined:
            _log.info("output:\n%s", combined)
        return False

    if timeout_seen or not wave_seen:
        if timeout_seen:
            _log.info("Timeout reached. Exiting. Failing test.")
        else:
            _log.info(
                "Loop timed out without receiving expected message. "
                "Failing test."
            )
        if combined:
            _log.info("output:\n%s", combined)
        return False

    all_output_string_found = True
    for check_str in check_list:
        if not (check_str.search(err_str)):
            all_output_string_found = False
            _log.info('"%s" Not Found in dump.', check_str.pattern)

    if not all_output_string_found and combined:
        _log.info("output:\n%s", combined)

    return all_output_string_found


def test_debug_info_comparison():
    check_list = [
        re.compile(re.escape(s))
        for s in ["c[gid] = a[gid] + b[gid] + (lds_check[0] >> 32);", "if (gid == 0)"]
    ]

    out_str_debug, err_str_debug, success_debug = run_and_communicate(
        test_name="10: debug info", args="1"
    )
    out_str_no_debug, err_str_no_debug, success_no_debug = run_and_communicate(
        test_name="10: no debug info", args="7"
    )

    if not (success_no_debug and success_debug):
        return False

    # check if string is in dissasembly of code with debug info but not in other one
    found_in_debug = False
    found_in_no_debug = False
    for check_str in check_list:
        pattern = re.compile(check_str)
        if pattern.search(err_str_debug):
            found_in_debug = True
        if pattern.search(err_str_no_debug):
            found_in_no_debug = True

    return found_in_debug and not found_in_no_debug


def test_eager_code_object_save():
    with tempfile.TemporaryDirectory() as tmpdir:
        run_and_communicate(
            test_name="eager code object save: no event, no code object",
            args="4",
            debug_agent_options=f"--save-code-objects={tmpdir}",
        )

        if len(os.listdir(tmpdir)) != 0:
            logging.getLogger("rocm-debug-agent-test").info(
                "Code object saved, while code object saving should have been lazy"
            )
            return False

        run_and_communicate(
            test_name="eager code object save: no event, no code object",
            args="4",
            debug_agent_options=f"--save-code-objects={tmpdir} -c",
        )

        if len(os.listdir(tmpdir)) == 0:
            logging.getLogger("rocm-debug-agent-test").info("Missing saved code objects")
            return False
    return True


def test_lazy_loading_and_eager_incompatible():
    out, err, success = run_and_communicate(
        test_name="eager code object save: no event, no code object",
        args="4",
        debug_agent_options=f"--load-all-code-objects --lazy",
    )
    check_list = [
        re.compile(s)
        for s in ['"--load-all-code-objects" and "--lazy" are mutually exclusive']
    ]
    if not success or not check_errors(check_list, out, err):
        logging.getLogger("rocm-debug-agent-test").info(
            "Failed to detect -c and -z incompatibility"
        )
        return False

    out, err, success = run_and_communicate(
        test_name="eager code object save: no event, no code object",
        args="4",
        debug_agent_options=f"--lazy --load-all-code-objects",
    )
    if not success or not check_errors(check_list, out, err):
        logging.getLogger("rocm-debug-agent-test").info(
            "Failed to detect -c and -z incompatibility"
        )
        return False
    return True


# ==============================================================================
# Test Definitions
#
# Each entry fully describes a test.  For tests with custom logic, 'function'
# points to the existing handler above.  For simple pattern-check tests that
# don't exist yet, 'patterns' alone is sufficient (handled by run_test).
#
# Supported keys:
#   name           - unique test identifier (used in output)
#   description    - human-readable summary
#   args           - argument(s) passed to the test binary
#   options        - ROCM_DEBUG_AGENT_OPTIONS value
#   timeout        - per-test timeout in seconds (default: DEFAULT_TIMEOUT)
#   patterns       - list of regex strings to match against stderr
#   function       - custom handler (no-arg callable); omit for default
#                    pattern-matching behaviour
#   abort_on_fail  - if True, abort the entire suite when this test fails
# ==============================================================================

TEST_DEFINITIONS = [
    {
        'name': 'test_baseline',
        'description': 'Default (no faults)',
        'args': '0',
        'options': '',
        'function': test_baseline,
        'timeout': 30,
    },
    {
        'name': 'test_assert_trap',
        'description': 'Assert trap (debug trap)',
        'args': '1',
        'options': '',
        'timeout': 30,
        'patterns': [
            r"HSA_STATUS_ERROR_EXCEPTION: An HSAIL operation resulted in a hardware exception\.",
            r"\(stopped, reason: ASSERT_TRAP\)",
            r"exec: (00000000)?00000001",
            r"s0:",
            r"v0:",
            r"0x0000: 22222222 11111111",
            r"Disassembly for function vector_add_assert_trap\(int\*, int\*, int\*\)",
        ],
    },
    {
        'name': 'test_memory_violation',
        'description': 'Memory violation',
        'args': '2',
        'options': '',
        'timeout': 30,
        'patterns': [
            r"\(stopped, reason: MEMORY_VIOLATION\)",
            r"exec: (ffffffff)?ffffffff",
            r"s0:",
            r"v0:",
            r"0x0000: 22222222 11111111",
            r"Disassembly for function vector_add_memory_fault\(int\*, int\*, int\*\)",
        ],
    },
    {
        'name': 'test_snapshot_code_object',
        'description': 'Snapshot code object on load',
        'args': '3',
        'options': '',
        'function': test_snapshot_code_object,
        'timeout': 30,
    },
    {
        'name': 'test_save_code_objects',
        'description': 'Save code objects to disk',
        'args': '4',
        'options': '',
        'function': test_save_code_objects,
        'timeout': 60,
    },
    {
        'name': 'test_output_redirection',
        'description': 'Output redirection (-o)',
        'args': '1',
        'options': '',
        'function': test_output_redirection,
        'timeout': 30,
        'patterns': [
            r"s0:",
            r"v0:",
            r"0x0000: 22222222 11111111",
            r"Disassembly for function vector_add_assert_trap\(int\*, int\*, int\*\)",
        ],
    },
    {
        'name': 'test_help',
        'description': 'Help (-h)',
        'args': '0',
        'options': '-h',
        'timeout': 10,
        'patterns': [
            r"ROCdebug-agent usage",
        ],
    },
    {
        'name': 'test_log_level',
        'description': 'Log level (-l info)',
        'args': '1',
        'options': '-l info',
        'timeout': 30,
        'patterns': [
            r"rocm-dbgapi",
        ],
    },
    {
        'name': 'test_all_waves',
        'description': 'All waves (--all)',
        'args': '5',
        'options': '--all',
        'timeout': 30,
        'patterns': [
            r"wave_1",
            r"wave_2",
            r"wave_3",
            r"wave_4",
            r"wave_5",
            r"wave_6",
            r"wave_7",
            r"wave_8",
        ],
    },
    {
        'name': 'test_sigquit',
        'description': 'SIGQUIT signal handling',
        'args': '6',
        'options': '',
        'function': test_sigquit,
        'timeout': 60,
        'patterns': [
            r"s0:",
            r"v0:",
            r"Disassembly for function sigquit_kern\(int\*\)",
        ],
    },
    {
        'name': 'test_debug_info',
        'description': 'Debug info comparison',
        'args': '1',
        'options': '',
        'function': test_debug_info_comparison,
        'timeout': 30,
        'patterns': [
            r"c\[gid\] = a\[gid\] \+ b\[gid\] \+ \(lds_check\[0\] >> 32\);",
            r"if \(gid == 0\)",
        ],
    },
    {
        'name': 'test_eager_vs_lazy',
        'description': 'Eager vs lazy code object save',
        'args': '4',
        'options': '',
        'function': test_eager_code_object_save,
        'timeout': 60,
    },
    {
        'name': 'test_lazy_loading_and_eager_incompatible',
        'description': 'Lazy/load-all incompatibility',
        'args': '4',
        'options': '',
        'function': test_lazy_loading_and_eager_incompatible,
        'timeout': 30,
        'patterns': [
            r'"--load-all-code-objects" and "--lazy" are mutually exclusive',
        ],
    },
]


def run_test(test_def):
    """Dispatch a single test definition.

    If 'function' is present, call it directly.
    Otherwise fall back to generic pattern matching against stderr.
    """
    if 'function' in test_def:
        return test_def['function']()

    # Generic path: run binary with args/options, check stderr patterns.
    check_list = [re.compile(s) for s in test_def.get('patterns', [])]
    out_str, err_str, success = run_and_communicate(
        test_name=test_def['name'],
        args=test_def['args'],
        debug_agent_options=test_def.get('options', ''),
        timeout=test_def.get('timeout', DEFAULT_TIMEOUT),
    )
    if not success:
        return False
    if not check_list:
        return True
    return check_errors(check_list, out_str, err_str)


unsupported_tests = []

test_success = True
failed_tests = []
total_pass = 0
total_fail = 0
total_unsupported = 0

for deferred_loading in (None, "1", "0"):
    with unittest.mock.patch.dict("os.environ"):
        if deferred_loading is None:
            mode_label = "HIP_ENABLE_DEFERRED_LOADING unset"
            if "HIP_ENABLE_DEFERRED_LOADING" in os.environ:
                del os.environ["HIP_ENABLE_DEFERRED_LOADING"]
        else:
            mode_label = f"HIP_ENABLE_DEFERRED_LOADING={deferred_loading}"
            os.environ["HIP_ENABLE_DEFERRED_LOADING"] = deferred_loading

        abort = False
        _log = logging.getLogger("rocm-debug-agent-test")
        for test_def in TEST_DEFINITIONS:
            name = test_def['name']
            desc = test_def['description']
            label = f"{name}: {desc} [{mode_label}]"

            _log.info("")
            _log.info("=" * 72)
            _log.info("  %s", label)
            _log.info("=" * 72)
            result = run_test(test_def)

            func = test_def.get('function')
            if func and func in unsupported_tests:
                verdict = f"UNSUPPORTED: {label}"
                print(verdict)
                _log.info(verdict)
                total_unsupported += 1
            elif result:
                verdict = f"PASS: {label}"
                print(verdict)
                _log.info(verdict)
                total_pass += 1
            else:
                verdict = f"FAIL: {label}"
                print(verdict)
                _log.info(verdict)
                total_fail += 1
                failed_tests.append(verdict)
                test_success = False

                if test_def.get('abort_on_fail', False):
                    msg = (
                        f"\n*** abort_on_fail set for {name} — "
                        "aborting remaining tests ***\n"
                    )
                    print(msg)
                    _log.info(msg)
                    abort = True
                    break

        if abort:
            break

total = total_pass + total_fail + total_unsupported
print()
print("=" * 60)
print()
print(f"Total:       {total}")
print(f"PASS:        {total_pass}")
print(f"FAIL:        {total_fail}")
print(f"UNSUPPORTED: {total_unsupported}")

if failed_tests:
    print()
    print("Failed:")
    for entry in failed_tests:
        print(f"  - {entry}")

print()
print("=" * 60)

if test_success:
    print("\nOVERALL: PASS")
else:
    print("\nOVERALL: FAIL")
    sys.exit(1)
