import enum

from .writer  import ResultWriter

class CompareModes(enum.Enum):
    undefined = enum.auto
    exact = enum.auto
    subset = enum.auto

class Fail(Exception):
    message: str = ""
    def __init__(self, message: str):
        self.message = message

class Fatal(Exception): pass

class Compare:
    """
    Compare two arrays
    This is a very specific implementation for netifd tests
    """
    _rw: ResultWriter

    def __init__(self, rw: ResultWriter) -> None:
        self._rw = rw

    def test_fatal(msg):
        raise Fatal("FATAL: %s" % msg)

    def test_error(self, msg):
        raise Fail(msg)

    def must_equal(self, actual, expected):
        if actual != expected:
            self.test_error(f"was '{actual}', expected '{expected}'")

    def test_missing_key(self, key):
        raise Fail(f"key '{key}' is missing")

    def test_contains_all(self, actual, expected):
        missing = []
        for key in expected:
            if key not in actual:
                missing.append(key)
        if missing:
            self.test_error(f"was '{actual}' must contain all of '{expected}'")

    def test_contains_none(self, actual, not_expected):
        missing = []
        for key in not_expected:
            if key in actual:
                missing.append(key)
        if missing:
            self.test_error(f"was '{actual}' must contain none of '{not_expected}'")

    def compare_array(self, actual, expected):
        conditions = list(filter(lambda x: isinstance(x, str) and x.startswith("__COND__SUBSET"), expected))
        expected = list(filter(lambda x: not isinstance(x, str) or not x.startswith("__COND__SUBSET"), expected))
        
        mode = CompareModes.undefined

        for cond in conditions:
            if cond == "__COND__SUBSET":
                if mode != CompareModes.undefined:
                    self.test_fatal("Multiple compare modes defined")
                mode = CompareModes.subset
            else:
                self.test_fatal("Unknown condition: %s" % repr(conditions))

        if mode == CompareModes.undefined:
            mode = CompareModes.exact

        actual = sorted(actual)
        expected = sorted(expected)

        if mode == CompareModes.subset:
            self.test_contains_all(actual, expected)
        elif mode == CompareModes.exact:
            self.must_equal(actual, expected)
        else:
            self.test_fatal("No compare mode")

    def compare_array_new(self, actual: list, test_info: dict):
        actual = sorted(actual)
        expected = sorted(test_info.get("expected", []))
        not_expected = sorted(test_info.get("not_expected", []))

        self.test_contains_all(actual, expected)
        self.test_contains_none(actual, not_expected)

    def compare_dict(self, actual, expected):
        for key, value in expected.items():
            if key == "_exact": continue

            if not key in actual:
                self.test_missing_key(key)

            if isinstance(value, dict) and value.get("_type", "") == "array":
                self.compare_array_new(actual[key], value)
            elif isinstance(value, dict) and not value.get("_exact", True):
                self.compare_dict(actual[key], value)
            elif isinstance(value, list):
                self.compare_array(actual[key], value)
            else:
                self.must_equal(actual[key], value)

    def compare(self, expected, actual, select):
        if not isinstance(actual, list) and not isinstance(expected, list):
            actual = [actual]
            expected = [expected]

        if select:
            if len(actual) > 1 or len(expected) > 1:
                self.test_fatal("Expected only one element in lists, when <select> is given")
            if len(actual) == 1:
                actual = actual[0][select]
            if len(expected) == 1:
                expected = expected[0][select]

        # Somewho ip -j can return empty dicts.... Let's just drop them
        actual = list(filter(lambda x: not isinstance(x, dict) or x, actual))

        with self._rw.start_test("Length Match"):
            if len(actual) != len(expected):
                self._rw.fatal("\n".join([
                        "Top level list lengths differ (was %d, expected %d)" % (len(actual), len(expected)),
                        "Actual: " + str(actual),
                        "Expected: " + str(expected)
                    ]))
                return

        error_count = 0
        for i, (exp, act) in enumerate(zip(expected, actual)):
            with self._rw.start_suite("Entry %d" % i):
                for key, value in exp.items():
                    with self._rw.start_test(key):
                        try:
                            if not key in act:
                                self.test_missing_key(key)

                            if isinstance(value, dict) and value.get("_type", "") == "array":
                                self.compare_array_new(act[key], value)
                            elif isinstance(value, dict) and not value.get("_exact", True):
                                self.compare_dict(act[key], value)
                            elif isinstance(value, list):
                                self.compare_array(act[key], value)
                            else:
                                self.must_equal(act[key], value)
                        except Fail as e:
                            self._rw.fail(e.message)
                            error_count += 1
                        except Fatal as e:
                            self._rw.fatal(e.message)
                            error_count += 1
                        except Exception:
                            raise

        return error_count
