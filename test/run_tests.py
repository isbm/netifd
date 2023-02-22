#!/usr/bin/env python3

import os
import sys
import logging
from argparse import ArgumentParser

from test_runner.loader import TestException
from test_runner.runner import TestRunner
from test_runner.writer import ResultWriter

def setup_logger():
    global logger
    logger = logging.Logger("run_tests.py")
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s %(levelname)7s %(message)s')
    ch.setFormatter(formatter)
    logger.addHandler(ch)

def main() -> int:
    parser = ArgumentParser()
    parser.add_argument("-o", "--output", default="results.xml", help="xunit xml output")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("-s", "--shell", action="store_true", help="Create shell after setup")
    parser.add_argument("tests", metavar="T", type=str, nargs="*", help="Tests to execute")
    args = parser.parse_args()
    setup_logger()
    logger.setLevel(logging.DEBUG if args.verbose else logging.INFO)

    tests = args.tests
    with ResultWriter(args.output, logger) as rw:
        try:
            runner = TestRunner(logger, os.path.join(os.path.dirname(os.path.realpath(__file__)), "testcases"))
            tests = list(map(
                lambda t: runner.get_suite(t) or t,
                tests
            ))
            unknown = list(filter(lambda x: isinstance(x, str), tests))
            if unknown:
                logger.error("Unknown tests: %s", ", ".join(unknown))
                rw.set_path(None)
                return -1
            runner.run(rw, tests, args.shell)
        except KeyboardInterrupt:
            rw.set_path(None)
            logger.error("Aborted by keyboard interrupt")
            return -1

    return min(rw.failed_test_count, 255)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except TestException as e:
        print(e)
        sys.exit(1)
