import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
from gi.repository import Gst, GstBase, GObject
import unittest


class AdapterTest(unittest.TestCase):

    def setUp(self):
        Gst.init(None)
        self.adapter = GstBase.Adapter.new()

    def tearDown(self):
        self.adapter = None

    def testAvailable(self):
        # starts empty
        self.assertEqual(self.adapter.available(), 0)
        self.assertEqual(self.adapter.available_fast(), 0)

        # let's give it 4 bytes
        self.adapter.push(Gst.Buffer.new_wrapped(b"1234"))
        self.assertEqual(self.adapter.available_fast(), 4)

        # let's give it another 5 bytes
        self.adapter.push(Gst.Buffer.new_wrapped(b"56789"))
        # we now have 9 bytes
        self.assertEqual(self.adapter.available(), 9)
        # but can only do a fast take of 4 bytes (the first buffer)
        self.assertEqual(self.adapter.available_fast(), 4)

    def testGetBuffer(self):
        self.adapter.push(Gst.Buffer.new_wrapped(b"0123456789"))

        # let's peek at 5 bytes
        b = self.adapter.get_buffer(5)
        self.assertEqual(b.get_size(), 5)
        self.assertEqual(b.extract_dup(0, b.get_size()), b"01234")

        # it's still 10 bytes big
        self.assertEqual(self.adapter.available(), 10)

        # if we try to peek more than what's available, we'll have an empty buffer
        b = self.adapter.get_buffer(11)
        self.assertEqual(b, None)

    def testFlush(self):
        self.adapter.push(Gst.Buffer.new_wrapped(b"0123456789"))
        self.assertEqual(self.adapter.available(), 10)

        self.adapter.flush(5)
        self.assertEqual(self.adapter.available(), 5)

        # it flushed the first 5 bytes
        b = self.adapter.get_buffer(5)
        self.assertEqual(b.extract_dup(0, b.get_size()), b"56789")

        self.adapter.flush(5)
        self.assertEqual(self.adapter.available(), 0)

    def testTakeBuffer(self):
        self.adapter.push(Gst.Buffer.new_wrapped(b"0123456789"))
        self.assertEqual(self.adapter.available(), 10)

        s = self.adapter.take_buffer(5)
        self.assertEqual(s.extract_dup(0, s.get_size()), b"01234")
        self.assertEqual(self.adapter.available(), 5)


if __name__ == "__main__":
    unittest.main()
