import time
import unittest

import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst

Gst.init(None)


class MyBin(Gst.Bin):
    _state_changed = False

    def __init__(self, name):
        super().__init__()
        self.set_property("name", name)

    def do_change_state(self, transition):
        if transition == Gst.StateChange.PAUSED_TO_PLAYING:
            self._state_changed = True
        return Gst.Bin.do_change_state(self, transition)


class BinSubclassTest(unittest.TestCase):
    def setUp(self):
        self.bin = MyBin("mybin")

    def tearDown(self):
        self.bin = None

    def testStateChange(self):
        assert self.bin.get_name() == "mybin"
        assert self.bin.__grefcount__ == 1

        # test get_state with no timeout
        ret, state, pending = self.bin.get_state(0)
        assert ret != Gst.StateChangeReturn.FAILURE
        assert self.bin.__grefcount__ == 1

        # set to playing
        self.bin.set_state(Gst.State.PLAYING)
        assert self.bin._state_changed

        # test get_state with no timeout
        ret, state, pending = self.bin.get_state(0)
        assert ret != Gst.StateChangeReturn.FAILURE

        if ret == Gst.StateChangeReturn.SUCCESS:
            assert state == Gst.State.PLAYING
            assert pending == Gst.State.VOID_PENDING

        # test get_state with a timeout
        ret, state, pending = self.bin.get_state(Gst.SECOND)
        assert ret != Gst.StateChangeReturn.FAILURE

        if ret == Gst.StateChangeReturn.SUCCESS:
            assert state == Gst.State.PLAYING
            assert pending == Gst.State.VOID_PENDING

        # back to NULL
        self.bin.set_state(Gst.State.NULL)


class BinAddRemove(unittest.TestCase):
    def setUp(self):
        self.bin = Gst.Bin.new("bin")

    def tearDown(self):
        self.bin = None

    def testError(self):
        src = Gst.ElementFactory.make("fakesrc", "src")
        sink = Gst.ElementFactory.make("fakesink", "sink")
        assert src.__grefcount__ == 1

        self.bin.add(src)
        assert src.__grefcount__ == 2
        with self.assertRaises(Gst.AddError):
            self.bin.add(src)
        self.bin.remove(sink)  # Should not raise, should just give gst warning

        self.bin.remove(src)
        self.bin.remove(src)  # Should not raise, should just give gst warning

    def testMany(self):
        src = Gst.ElementFactory.make("fakesrc")
        sink = Gst.ElementFactory.make("fakesink")
        self.bin.add(src, sink)
        with self.assertRaises(Gst.AddError):
            self.bin.add(src, sink)
        # FIXME: We should be able to do self.bin.remove(src, sink) instead of two calls
        self.bin.remove(src)
        self.bin.remove(sink)


class Preroll(unittest.TestCase):
    def setUp(self):
        self.bin = Gst.Bin.new("bin")

    def tearDown(self):
        # Wait for state change thread to settle down
        while self.bin.__grefcount__ > 1:
            time.sleep(0.1)
        assert self.bin.__grefcount__ == 1
        self.bin = None

    def testFake(self):
        src = Gst.ElementFactory.make("fakesrc")
        sink = Gst.ElementFactory.make("fakesink")
        self.bin.add(src)

        # bin will go to paused, src pad task will start and error out
        self.bin.set_state(Gst.State.PAUSED)
        ret, state, pending = self.bin.get_state(Gst.CLOCK_TIME_NONE)
        assert ret == Gst.StateChangeReturn.SUCCESS
        assert state == Gst.State.PAUSED
        assert pending == Gst.State.VOID_PENDING

        # adding the sink will cause the bin to go in preroll mode
        self.bin.add(sink)
        sink.set_state(Gst.State.PAUSED)
        ret, state, pending = self.bin.get_state(0)
        assert ret == Gst.StateChangeReturn.ASYNC
        assert state == Gst.State.PAUSED
        assert pending == Gst.State.PAUSED

        # to actually complete preroll, we need to link and re-enable fakesrc
        src.set_state(Gst.State.READY)
        src.link(sink)
        src.set_state(Gst.State.PAUSED)
        ret, state, pending = self.bin.get_state(Gst.CLOCK_TIME_NONE)
        assert ret == Gst.StateChangeReturn.SUCCESS
        assert state == Gst.State.PAUSED
        assert pending == Gst.State.VOID_PENDING

        self.bin.set_state(Gst.State.NULL)
        self.bin.get_state(Gst.CLOCK_TIME_NONE)


class ConstructorTest(unittest.TestCase):
    def testGood(self):
        Gst.Bin.new()
        Gst.Bin.new(None)
        Gst.Bin.new("")
        Gst.Bin.new("myname")

    def testBad(self):
        with self.assertRaises(TypeError):
            Gst.Bin.new(0)
        with self.assertRaises(TypeError):
            Gst.Bin.new(Gst.Bin.new())


if __name__ == "__main__":
    unittest.main()
