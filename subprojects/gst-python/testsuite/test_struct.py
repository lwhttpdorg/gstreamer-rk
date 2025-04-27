import sys
from gi.repository import Gst
import unittest

Gst.init(None)


class StructureTest(unittest.TestCase):
    def setUp(self):
        self.struct = Gst.Structure.from_string(
            'video/x-raw,width=10,foo="bar",pixel-aspect-ratio=1/2,framerate=5/1,boolean=true'
        )[0]

    def testName(self):
        assert self.struct.get_name() == "video/x-raw"
        self.struct.set_name("foobar")
        assert self.struct.get_name() == "foobar"

    def testInt(self):
        assert self.struct.has_field("width")
        assert isinstance(self.struct.get_value("width"), int)
        assert self.struct.get_value("width") == 10
        self.struct.set_value("width", 5)
        assert self.struct.has_field("width")
        assert isinstance(self.struct.get_value("width"), int)
        assert self.struct.get_value("width") == 5

    def testString(self):
        assert self.struct.has_field("foo")
        assert isinstance(self.struct.get_value("foo"), str)
        assert self.struct.get_value("foo") == "bar"
        self.struct.set_value("foo", "baz")
        assert self.struct.has_field("foo")
        assert isinstance(self.struct.get_value("foo"), str)
        assert self.struct.get_value("foo") == "baz"

    def testBoolean(self):
        assert self.struct.has_field("boolean")
        boolean_result = self.struct.get_boolean("boolean")

        # Check if boolean_result is a tuple and unpack it
        if isinstance(boolean_result, tuple):
            success, boolean_value = boolean_result
            assert success, f"Failed to get boolean value: {boolean_result}"
        else:
            boolean_value = boolean_result

        assert isinstance(
            boolean_value, bool
        ), f"Expected bool, got {type(boolean_value)}"
        assert boolean_value == True

        self.struct.set_value("boolean", False)
        assert self.struct.has_field("boolean")
        new_boolean_result = self.struct.get_boolean("boolean")

        # Check if new_boolean_result is a tuple and unpack it
        if isinstance(new_boolean_result, tuple):
            success, new_boolean_value = new_boolean_result
            assert success, f"Failed to get new boolean value: {new_boolean_result}"
        else:
            new_boolean_value = new_boolean_result

        assert isinstance(
            new_boolean_value, bool
        ), f"Expected bool, got {type(new_boolean_value)}"
        assert new_boolean_value == False

    def testCreateInt(self):
        self.struct.set_value("integer", 5)
        assert self.struct.has_field("integer")
        assert isinstance(self.struct.get_value("integer"), int)
        assert self.struct.get_value("integer") == 5

    def testGstValue(self):
        s = self.struct

        # Gst.Fourcc is not available, skipping fourcc test
        s.set_value("frac", Gst.Fraction(3, 4))
        frac_result = s.get_fraction("frac")

        if isinstance(frac_result, tuple):
            success, num, denom = frac_result
            assert success, f"Failed to get fraction: {frac_result}"
            assert (num, denom) == (3, 4), f"Expected 3/4, got {num}/{denom}"
        else:
            assert frac_result == Gst.Fraction(
                3, 4
            ), f"Expected Gst.Fraction(3, 4), got {frac_result}"
        s.set_value(
            "fracrange", Gst.FractionRange(Gst.Fraction(0, 1), Gst.Fraction(25, 3))
        )
        fracrange = s.get_value("fracrange")
        assert isinstance(fracrange, Gst.FractionRange)
        assert fracrange.start == Gst.Fraction(0, 1)
        assert fracrange.stop == Gst.Fraction(25, 3)
        s.set_value("intrange", Gst.Int64Range(range(5, 21)))
        intrange = s.get_value("intrange")
        assert isinstance(intrange, Gst.Int64Range)
        # Replace get_min() and get_max() with range attribute
        assert intrange.range.start == 5
        assert intrange.range.stop == 21
        s.set_value("doublerange", Gst.DoubleRange(6.0, 21.0))
        doublerange = s.get_value("doublerange")
        assert isinstance(doublerange, Gst.DoubleRange)
        assert doublerange.start == 6.0
        assert doublerange.stop == 21.0
        s.set_value("fixedlist", (4, 5, 6))
        assert isinstance(s.get_value("fixedlist"), tuple)
        assert s.get_value("fixedlist") == (4, 5, 6)
        s.set_value("list", [4, 5, 6])
        assert isinstance(s.get_value("list"), list)
        assert s.get_value("list") == [4, 5, 6]
        s.set_value("boolean", True)
        assert isinstance(s.get_value("boolean"), bool)
        assert s.get_value("boolean") == True

        # Recursive tests
        s.set_value("rflist", ([(["a", "b"], ["c", "d"]), "e"], ["f", "g"]))
        assert s.get_value("rflist") == ([(["a", "b"], ["c", "d"]), "e"], ["f", "g"])
        s.set_value("rlist", [([(["a", "b"], ["c", "d"]), "e"], ["f", "g"]), "h"])
        assert s.get_value("rlist") == [
            ([(["a", "b"], ["c", "d"]), "e"], ["f", "g"]),
            "h",
        ]

    def testStructureChange(self):
        actual_framerate = self.struct.get_fraction("framerate")

        # Check if actual_framerate is a tuple and unpack it
        if isinstance(actual_framerate, tuple):
            success, num, denom = actual_framerate
            assert success, f"Failed to get framerate: {actual_framerate}"
            assert num == 5 and denom == 1, f"Expected 5/1, got {num}/{denom}"
        else:
            assert (
                actual_framerate.num == 5 and actual_framerate.denom == 1
            ), f"Expected 5/1, got {actual_framerate}"

        self.struct.set_value("framerate", Gst.Fraction(10, 1))
        new_framerate = self.struct.get_fraction("framerate")

        if isinstance(new_framerate, tuple):
            success, num, denom = new_framerate
            assert success, f"Failed to get new framerate: {new_framerate}"
            assert num == 10 and denom == 1, f"Expected 10/1, got {num}/{denom}"
        else:
            assert new_framerate == Gst.Fraction(
                10, 1
            ), f"Expected 10/1, got {new_framerate}"

        self.struct.set_value("pixel-aspect-ratio", Gst.Fraction(4, 2))
        par = self.struct.get_fraction("pixel-aspect-ratio")

        if isinstance(par, tuple):
            success, num, denom = par
            assert success, f"Failed to get pixel aspect ratio: {par}"
            assert num == 2 and denom == 1, f"Expected 2/1, got {num}/{denom}"
        else:
            assert par.num == 2 and par.denom == 1, f"Expected 2/1, got {par}"

    def testKeys(self):
        k = list(self.struct.keys())
        assert k
        assert len(k) == 5
        assert "width" in k
        assert "foo" in k
        assert "framerate" in k
        assert "pixel-aspect-ratio" in k
        assert "boolean" in k


if __name__ == "__main__":
    unittest.main()
