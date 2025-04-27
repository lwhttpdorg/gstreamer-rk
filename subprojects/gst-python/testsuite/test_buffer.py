import unittest
import gc
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst



class BufferTest(unittest.TestCase):
    def setUp(self):
        Gst.init(None)

    def testBufferBuffer(self):
        buf = Gst.Buffer.new_wrapped(b"test")
        map_info = buf.map(Gst.MapFlags.READ)
        
        success, info = map_info
        self.assertTrue(success)
        self.assertIsInstance(info, Gst.MapInfo)
        
        # Access the data using the 'data' attribute of MapInfo
        data = info.data
        self.assertEqual(bytes(data), b"test")
        
        # Don't forget to unmap the buffer
        buf.unmap(info)

    @unittest.expectedFailure
    def testBufferStr(self):
        buffer = Gst.Buffer.new_wrapped(b"test")
        assert str(buffer) == "test"

    def testBufferAlloc(self):
        bla = b"mooooooo"
        buffer = Gst.Buffer.new_wrapped(bla + b"12345")
        gc.collect()
        
        map_result = buffer.map(Gst.MapFlags.READ)
        success, info = map_result
        
        self.assertTrue(success, "Failed to map buffer")
        self.assertIsInstance(info, Gst.MapInfo)
        
        self.assertEqual(bytes(info.data), b"mooooooo12345")
        
        # Don't forget to unmap the buffer
        buffer.unmap(info)

    def testBufferBadConstructor(self):
        with self.assertRaises(TypeError):
            Gst.Buffer.new_wrapped(b"test", 0)

    def testBufferStrNull(self):
        test_string = b"t\0e\0s\0t\0"
        buffer = Gst.Buffer.new_wrapped(test_string)
        
        map_info = buffer.map(Gst.MapFlags.READ)
        success, info = map_info
        self.assertTrue(success)
        self.assertIsInstance(info, Gst.MapInfo)
        
        # Access the data using the 'data' attribute of MapInfo
        data = info.data
        self.assertEqual(bytes(data), test_string)
        
        # Don't forget to unmap the buffer
        buffer.unmap(info)

    def testBufferSize(self):
        test_string = b"a little string"
        buffer = Gst.Buffer.new_wrapped(test_string)
        assert buffer.get_size() == len(test_string)

    def testBufferCreateSub(self):
        s = b"".join(f"{i:02d}".encode() for i in range(64))
        buffer = Gst.Buffer.new_wrapped(s)
        assert buffer.get_size() == 128

        sub = buffer.copy_region(Gst.BufferCopyFlags.MEMORY, 16, 16)
        assert sub.get_size() == 16
        assert sub.extract_dup(0, sub.get_size()) == buffer.extract_dup(16, 16)
        assert sub.pts == Gst.CLOCK_TIME_NONE

    # FIXME: Removed tests for Buffer merge, join & span from gst-0.1x API
    # as there's no equivalent API.

    def testBufferCopy(self):
        s = b"test_vector"
        buffer = Gst.Buffer.new_wrapped(s)
        sub = buffer.copy_region(Gst.BufferCopyFlags.MEMORY, 0, buffer.get_size())
        assert sub.get_size() == buffer.get_size()
        out = sub.copy()
        assert out.get_size() == sub.get_size()

        # Update this part
        buffer_map = buffer.map(Gst.MapFlags.READ)
        out_map = out.map(Gst.MapFlags.READ)
        
        assert buffer_map[0]  # Check if mapping was successful
        assert out_map[0]  # Check if mapping was successful
        
        buffer_data = buffer_map[1].data
        out_data = out_map[1].data
        
        assert bytes(buffer_data) == bytes(out_data)

        # Don't forget to unmap the buffers
        buffer.unmap(buffer_map[1])
        out.unmap(out_map[1])

    # FIXME: Figure out how to write buffers in python
    @unittest.expectedFailure
    def testBufferWriteFail(self):
        s = b"test_vector"
        buffer = Gst.Buffer.new_wrapped(s)
        buffer[5] = b"w"
        assert buffer.map(Gst.MapFlags.READ).data.tobytes() == b"test_wector"

    def testBufferFlagOps(self):
        buffer = Gst.Buffer.new()

        # Off by default
        assert not buffer.get_flags() & Gst.BufferFlags.LIVE
        assert not buffer.get_flags() & Gst.BufferFlags.DISCONT

        # Try switching on and off LIVE flag
        buffer.set_flags(Gst.BufferFlags.LIVE)
        assert buffer.get_flags() & Gst.BufferFlags.LIVE
        buffer.unset_flags(Gst.BufferFlags.LIVE)
        assert not buffer.get_flags() & Gst.BufferFlags.LIVE

        # Try switching on and off DISCONT flag
        buffer.set_flags(Gst.BufferFlags.DISCONT)
        assert buffer.get_flags() & Gst.BufferFlags.DISCONT
        buffer.unset_flags(Gst.BufferFlags.DISCONT)
        assert not buffer.get_flags() & Gst.BufferFlags.DISCONT

        # Test multiple flags
        buffer.set_flags(Gst.BufferFlags.LIVE | Gst.BufferFlags.DISCONT)
        assert buffer.get_flags() & Gst.BufferFlags.LIVE
        assert buffer.get_flags() & Gst.BufferFlags.DISCONT
        buffer.unset_flags(Gst.BufferFlags.LIVE | Gst.BufferFlags.DISCONT)
        assert not buffer.get_flags() & Gst.BufferFlags.LIVE
        assert not buffer.get_flags() & Gst.BufferFlags.DISCONT

    def testAttrFlags(self):
        buffer = Gst.Buffer.new()
        assert hasattr(buffer, "get_flags")
        assert isinstance(buffer.get_flags(), Gst.BufferFlags)

    def testAttrTimestamp(self):
        buffer = Gst.Buffer.new()
        assert buffer.pts == Gst.CLOCK_TIME_NONE
        buffer.pts = 0
        assert buffer.pts == 0
        buffer.pts = 2**64 - 1
        assert buffer.pts == 2**64 - 1

    def testAttrDuration(self):
        buffer = Gst.Buffer.new()
        assert hasattr(buffer, "duration")
        assert isinstance(buffer.duration, int)

        assert buffer.duration == Gst.CLOCK_TIME_NONE
        buffer.duration = 0
        assert buffer.duration == 0
        buffer.duration = 2**64 - 1
        assert buffer.duration == 2**64 - 1

    def testAttrOffset(self):
        buffer = Gst.Buffer.new()
        assert hasattr(buffer, "offset")
        assert isinstance(buffer.offset, int)

        assert buffer.offset == Gst.CLOCK_TIME_NONE
        buffer.offset = 0
        assert buffer.offset == 0
        buffer.offset = 2**64 - 1
        assert buffer.offset == 2**64 - 1

    def testAttrOffset_end(self):
        buffer = Gst.Buffer.new()
        assert hasattr(buffer, "offset_end")
        assert isinstance(buffer.offset_end, int)

        assert buffer.offset_end == Gst.CLOCK_TIME_NONE
        buffer.offset_end = 0
        assert buffer.offset_end == 0
        buffer.offset_end = 2**64 - 1
        assert buffer.offset_end == 2**64 - 1


if __name__ == "__main__":
    unittest.main()
