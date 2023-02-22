import os
import time
from collections import deque
from datetime import datetime
import xml.etree.ElementTree as ET

from logging import Logger
from typing import Deque

class ResultSuiteContext():
    _parent: 'ResultWriter'
    _start_time: float

    def __init__(self, parent) -> None:
        self._parent = parent
        self._start_time = time.monotonic()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self._parent.end_suite(self._start_time)

class ResultTestContext():
    _parent: 'ResultWriter'
    _start_time: float

    def __init__(self, parent) -> None:
        self._parent = parent
        self._start_time = time.monotonic()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self._parent.end_test(self._start_time)

class ResultWriter():
    _logger: Logger
    _path: str
    _tree: ET.ElementTree
    _root: ET.Element
    _current_suite: ET.Element
    _suite_stack: Deque[str]
    _current_test: ET.Element
    _start_time: float

    def __init__(self, path: str, logger: Logger) -> None:
        self._logger = logger
        self._path = path
        self._tree = ET.ElementTree()
        self._root = ET.Element("testsuites")
        self._root.set("tests", "0")
        self._root.set("failures", "0")
        self._root.set("errors", "0")
        self._tree._setroot(self._root)
        self._current_suite = None
        self._suite_stack = deque()
        self._current_test = None
        self._start_time = time.monotonic()
        if os.path.isfile(path):
            os.remove(path)

    def set_path(self, path: str) -> None:
        self._path = path

    def start_suite(self, name: str) -> ResultSuiteContext:
        if self._current_suite:
            self._suite_stack.append(name)
        else:
            new_suite = ET.SubElement(self._root, "testsuite")
            new_suite.set("name", name)
            new_suite.set("tests", "0")
            new_suite.set("failures", "0")
            new_suite.set("errors", "0")
            new_suite.set("timestamp", datetime.now().isoformat())
            self._current_suite = new_suite
            self._logger.info(f"Starting test {name}")
        return ResultSuiteContext(self)

    def end_suite(self, start_time: float):
        if self._suite_stack:
            self._suite_stack.pop()
        else:
            self._logger.info("Finished test %s (executed %s test, %s failures, %s errors)" % (
                self._current_suite.get("name"),
                self._current_suite.get("tests"),
                self._current_suite.get("failures"),
                self._current_suite.get("errors")
            ))
            self._current_suite.set("time", "%0.2f" % (time.monotonic() - start_time))
            self._current_suite = None

    def _propagate_inc(self, field: str):
        self._current_suite.set(field, str(int(self._current_suite.get(field, "0")) + 1))
        self._root.set(field, str(int(self._root.get(field, "0")) + 1))

    def start_test(self, name: str) -> ResultTestContext:
        self._propagate_inc("tests")
        self._current_test = ET.SubElement(self._current_suite, "testcase")
        self._current_test.set("name", ".".join([*self._suite_stack, name]))
        return ResultTestContext(self)

    def end_test(self, start_time: float):
        self._logger.debug("Test '%s' succeeded", self._current_test.get("name"))
        self._current_test.set("time", "%0.2f" % (time.monotonic() - start_time))
        self._current_test = None

    def fail(self, message: str):
        self._propagate_inc("failures")
        ET.SubElement(self._current_test, "failure").text = message
        self._logger.error("Test %s failed: %s", self._current_test.get("name"), message)

    def fatal(self, message: str):
        self._propagate_inc("errors")
        ET.SubElement(self._current_test, "error").text = message
        self._logger.error("Test '%s' failed: %s", self._current_test.get("name"), message)

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self._root.set("time", "%0.2f" % (time.monotonic() - self._start_time))
        if self._path:
            ET.indent(self._tree)
            self._tree.write(self._path, "utf-8", True)
            self._logger.info("Finished all tests(executed %s test, %s failures, %s errors)" % (
                self._root.get("tests"),
                self._root.get("failures"),
                self._root.get("errors")
            ))


    @property
    def failed_test_count(self):
        return int(self._root.get("failures")) + int(self._root.get("errors"))
